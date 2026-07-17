package main

import (
	"errors"
	"fmt"
	"log"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/bluenviron/gortsplib/v5"
	"github.com/bluenviron/gortsplib/v5/pkg/base"
	"github.com/bluenviron/gortsplib/v5/pkg/description"
	"github.com/bluenviron/gortsplib/v5/pkg/format"
	"github.com/bluenviron/gortsplib/v5/pkg/headers"
	"github.com/bluenviron/gortsplib/v5/pkg/liberrors"
	"github.com/pion/rtp"
)

type pathStream struct { mu sync.RWMutex; stream *gortsplib.ServerStream; metadataStarted atomic.Bool }
type handler struct { paths map[string]*pathStream }

const (
	relayWriteQueueSize = 65536
	// DTT waits only a few seconds for frames after SetVideoEncoderConfiguration.
	// MediaMTX restarts the publisher during that operation, so an 8s backoff
	// can outlive the test window and produce a false zero-frame failure.
	maxReconnectDelay   = 2 * time.Second
)

// Profile M/T require Digest authentication at the RTSP layer.  Keep the
// relay credentials aligned with the ONVIF mock's default device account.
const rtspUser = "admin"
const rtspPassword = "admin123"

func ok() *base.Response { return &base.Response{StatusCode: base.StatusOK} }
func unauthorized() *base.Response { return &base.Response{StatusCode: base.StatusUnauthorized} }
func ptrProtocolTCP() *gortsplib.Protocol { p := gortsplib.ProtocolTCP; return &p }
func (h *handler) auth(c *gortsplib.ServerConn, req *base.Request) bool {
 authorization := ""
 for name, values := range req.Header {
  if strings.EqualFold(name, "Authorization") && len(values) > 0 { authorization = values[0]; break }
 }
 uri := "<nil>"
 if req.URL != nil { uri = req.URL.String() }
 verified := c.VerifyCredentials(req, rtspUser, rtspPassword)
 log.Printf("RTSP auth method=%s uri=%s verified=%t authorization=%q", req.Method, uri, verified, authorization)
 if verified { return true }
 return false
}
func (h *handler) get(p string) *pathStream { return h.paths[strings.Trim(p, "/")] }

func (h *handler) OnDescribe(c *gortsplib.ServerHandlerOnDescribeCtx) (*base.Response, *gortsplib.ServerStream, error) {
 if !h.auth(c.Conn, c.Request) { return unauthorized(), nil, liberrors.ErrServerAuth{} }
 ps := h.get(c.Path); if ps == nil { return &base.Response{StatusCode: base.StatusNotFound}, nil, nil }
	ps.mu.RLock(); defer ps.mu.RUnlock()
	if ps.stream == nil { return &base.Response{StatusCode: base.StatusServiceUnavailable}, nil, nil }
	log.Printf("DESCRIBE path=%s", strings.Trim(c.Path, "/")); return ok(), ps.stream, nil
}
func (h *handler) OnSetup(c *gortsplib.ServerHandlerOnSetupCtx) (*base.Response, *gortsplib.ServerStream, error) {
 if !h.auth(c.Conn, c.Request) { return unauthorized(), nil, liberrors.ErrServerAuth{} }
 ps := h.get(c.Path); if ps == nil { return &base.Response{StatusCode: base.StatusNotFound}, nil, nil }
	ps.mu.RLock(); defer ps.mu.RUnlock()
	if ps.stream == nil { return &base.Response{StatusCode: base.StatusServiceUnavailable}, nil, nil }
	log.Printf("SETUP path=%s transport=%v", strings.Trim(c.Path, "/"), c.Transport); return ok(), ps.stream, nil
}
func (h *handler) OnPlay(c *gortsplib.ServerHandlerOnPlayCtx) (*base.Response, error) {
 if !h.auth(c.Conn, c.Request) { return unauthorized(), liberrors.ErrServerAuth{} }
 log.Printf("PLAY path=%s", strings.Trim(c.Path, "/")); return ok(), nil
}
func (h *handler) OnGetParameter(*gortsplib.ServerHandlerOnGetParameterCtx) (*base.Response, error) { return ok(), nil }
func (h *handler) OnSetParameter(*gortsplib.ServerHandlerOnSetParameterCtx) (*base.Response, error) { return ok(), nil }

func setStream(server *gortsplib.Server, ps *pathStream, desc *description.Session) *gortsplib.ServerStream {
 ps.mu.Lock(); defer ps.mu.Unlock()
	if ps.stream != nil {
		// Keep the original media pointers. ServerStream indexes its internal
		// state by those pointers; replacing Desc at runtime causes DESCRIBE to
		// dereference a missing media state and panic. A reconnect can still
		// publish packets using the existing stable description.
		return ps.stream
	}
	st := &gortsplib.ServerStream{Server: server, Desc: desc}
	if err := st.Initialize(); err != nil { log.Fatalf("stream initialize: %v", err) }
	ps.stream = st; return st
}

func relayPath(server *gortsplib.Server, ps *pathStream, path string) {
	src := "rtsp://127.0.0.1:8554/" + path
	delay := time.Second
	for {
		if err := relayOnce(server, ps, path, src); err != nil {
			log.Printf("relay %s: %v; reconnecting in %s", path, err, delay)
			time.Sleep(delay)
			if delay < maxReconnectDelay { delay *= 2; if delay > maxReconnectDelay { delay = maxReconnectDelay } }
			continue
		}
		delay = time.Second
	}
}

func relayOnce(server *gortsplib.Server, ps *pathStream, path, src string) error {
	u, err := base.ParseURL(src); if err != nil { return err }
	c := &gortsplib.Client{Scheme: u.Scheme, Host: u.Host, Protocol: ptrProtocolTCP(), ReadTimeout: 10*time.Second, WriteTimeout: 10*time.Second}
	if err = c.Start(); err != nil { return err }; defer c.Close()
	desc, _, err := c.Describe(u); if err != nil { return err }
	if err = c.SetupAll(desc.BaseURL, desc.Medias); err != nil { return err }
	var metadataTrack *description.Media
	if path == "main" {
		metadataTrack = metadataMedia()
		desc.Medias = append(desc.Medias, metadataTrack)
	}
	st := setStream(server, ps, desc); log.Printf("relay ready path=%s from=%s", path, src)
	if metadataTrack != nil { startMetadata(server, ps, metadataTrack, path) }
	var unknownMediaCount uint64
	formatSummary := func(media *description.Media) string {
		if media == nil { return "<nil>" }
		parts := make([]string, 0, len(media.Formats))
		for _, f := range media.Formats { parts = append(parts, fmt.Sprintf("pt=%d rtp=%q", f.PayloadType(), f.RTPMap())) }
		return fmt.Sprintf("type=%s formats=[%s]", media.Type, strings.Join(parts, ", "))
	}
	stableMedia := func(in *description.Media) *description.Media {
		ps.mu.RLock(); defer ps.mu.RUnlock()
		for _, candidate := range st.Desc.Medias {
			if candidate.Type != in.Type { continue }
			// MediaMTX can rebuild an SDP on reconnect and change format order
			// or add auxiliary formats. Match any compatible format instead of
			// requiring the complete format slice to be identical.
			for _, current := range candidate.Formats {
				for _, incoming := range in.Formats {
					if current.PayloadType() == incoming.PayloadType() && current.RTPMap() == incoming.RTPMap() { return candidate }
				}
			}
		}
		return nil
	}
	c.OnPacketRTPAny(func(media *description.Media, _ format.Format, pkt *rtp.Packet) {
		// Upstream reconnects may briefly deliver a payload type that is not
		// present in the stable ServerStream description. gortsplib expects the
		// format lookup to succeed and would otherwise dereference nil.
		defer func() {
			if r := recover(); r != nil {
				log.Printf("relay write %s: recovered from format mismatch: %v", path, r)
			}
		}()
		target := stableMedia(media)
		if target == nil {
			n := atomic.AddUint64(&unknownMediaCount, 1)
			if n <= 5 || n%1000 == 0 {
				ps.mu.RLock()
				stable := make([]string, 0, len(st.Desc.Medias))
				for _, candidate := range st.Desc.Medias { stable = append(stable, formatSummary(candidate)) }
				ps.mu.RUnlock()
				log.Printf("relay write %s: unknown media count=%d incoming={%s} stable=[%s] packet_pt=%d", path, n, formatSummary(media), strings.Join(stable, "; "), pkt.PayloadType)
			}
			return
		}
		matchedPayload := false
		for _, f := range target.Formats {
			if f.PayloadType() == pkt.PayloadType { matchedPayload = true; break }
		}
		if !matchedPayload {
			log.Printf("relay write %s: unsupported payload type %d", path, pkt.PayloadType)
			return
		}
		// A slow DTT client can temporarily fill gortsplib's per-session queue.
		// Drop that packet and keep the upstream relay alive; returning from the
		// callback quickly is important when several DTT sessions are active.
		if err := st.WritePacketRTP(target, pkt); err != nil {
			var queueFull liberrors.ErrServerWriteQueueFull
			if !errors.As(err, &queueFull) {
				log.Printf("relay write %s: %v", path, err)
			}
		}
	})
	if _, err = c.Play(nil); err != nil { return err }; return c.Wait()
}

func metadataXML() []byte {
	now := time.Now().UTC().Format(time.RFC3339Nano)
	return []byte(fmt.Sprintf(`<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema"><tt:VideoAnalytics><tt:Frame UtcTime="%s"><tt:Object ObjectId="1"><tt:Appearance><tt:Shape><tt:BoundingBox left="0.10" top="0.10" right="0.40" bottom="0.60"/></tt:Shape><tt:Class><tt:Type Likelihood="0.90">Human</tt:Type></tt:Class></tt:Appearance></tt:Object></tt:Frame></tt:VideoAnalytics></tt:MetadataStream>`, now))
}
func metadataMedia() *description.Media {
	f := &format.Generic{PayloadTyp: 107, RTPMa: "vnd.onvif.metadata/90000", FMT: map[string]string{}}
	if err := f.Init(); err != nil { log.Fatalf("metadata format: %v", err) }
	return &description.Media{Type: description.MediaTypeApplication, Profile: headers.TransportProfileAVP, Control: "trackID=metadata", Formats: []format.Format{f}}
}

func startMetadata(server *gortsplib.Server, ps *pathStream, media *description.Media, label string) {
	if !ps.metadataStarted.CompareAndSwap(false, true) { return }
	st := setStream(server, ps, &description.Session{Title: "ONVIF metadata", Medias: []*description.Media{media}})
	go func() {
		ticker := time.NewTicker(500*time.Millisecond); defer ticker.Stop(); var seq uint32; start := time.Now()
		for range ticker.C {
			pkt := &rtp.Packet{Header: rtp.Header{Version: 2, Marker: true, PayloadType: 107, SequenceNumber: uint16(atomic.AddUint32(&seq, 1)), Timestamp: uint32(time.Since(start).Seconds()*90000), SSRC: 0x4d455441}, Payload: metadataXML()}
			if err := st.WritePacketRTP(media, pkt); err != nil { log.Printf("metadata write path=%s: %v", label, err) }
		}
	}()
	log.Printf("metadata ready path=%s", label)
}

func main() {
	paths := map[string]*pathStream{"main": {}, "jpeg": {}, "sub1": {}, "sub2": {}, "metadata": {}}
	// DTT consumes the 4K stream over RTSP/TCP. The gortsplib default queue
	// (256 packets) is too small for a short client-side scheduling stall and
	// causes the server to close the session with "write queue is full".
	h := &handler{paths: paths}; srv := &gortsplib.Server{RTSPAddress: ":8555", UDPRTPAddress: ":8050", UDPRTCPAddress: ":8051", Handler: h, WriteQueueSize: relayWriteQueueSize, ReadTimeout: 10*time.Second, WriteTimeout: 10*time.Second, IdleTimeout: 60*time.Second}; if err := srv.Start(); err != nil { log.Fatalf("server start: %v", err) }
	startMetadata(srv, paths["metadata"], metadataMedia(), "metadata"); for _, p := range []string{"main", "jpeg", "sub1", "sub2"} { go relayPath(srv, paths[p], p) }; log.Printf("RTSP tunnel relay listening on :8555"); if err := srv.Wait(); err != nil { log.Fatal(err) }
}
