package main

import (
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
	"github.com/pion/rtp"
)

type pathStream struct { mu sync.RWMutex; stream *gortsplib.ServerStream }
type handler struct { paths map[string]*pathStream }

func ok() *base.Response { return &base.Response{StatusCode: base.StatusOK} }
func (h *handler) get(p string) *pathStream { return h.paths[strings.Trim(p, "/")] }

func (h *handler) OnDescribe(c *gortsplib.ServerHandlerOnDescribeCtx) (*base.Response, *gortsplib.ServerStream, error) {
	ps := h.get(c.Path); if ps == nil { return &base.Response{StatusCode: base.StatusNotFound}, nil, nil }
	ps.mu.RLock(); defer ps.mu.RUnlock()
	if ps.stream == nil { return &base.Response{StatusCode: base.StatusServiceUnavailable}, nil, nil }
	log.Printf("DESCRIBE path=%s", strings.Trim(c.Path, "/")); return ok(), ps.stream, nil
}
func (h *handler) OnSetup(c *gortsplib.ServerHandlerOnSetupCtx) (*base.Response, *gortsplib.ServerStream, error) {
	ps := h.get(c.Path); if ps == nil { return &base.Response{StatusCode: base.StatusNotFound}, nil, nil }
	ps.mu.RLock(); defer ps.mu.RUnlock()
	if ps.stream == nil { return &base.Response{StatusCode: base.StatusServiceUnavailable}, nil, nil }
	log.Printf("SETUP path=%s transport=%v", strings.Trim(c.Path, "/"), c.Transport); return ok(), ps.stream, nil
}
func (h *handler) OnPlay(c *gortsplib.ServerHandlerOnPlayCtx) (*base.Response, error) { log.Printf("PLAY path=%s", strings.Trim(c.Path, "/")); return ok(), nil }
func (h *handler) OnGetParameter(*gortsplib.ServerHandlerOnGetParameterCtx) (*base.Response, error) { return ok(), nil }
func (h *handler) OnSetParameter(*gortsplib.ServerHandlerOnSetParameterCtx) (*base.Response, error) { return ok(), nil }

func setStream(server *gortsplib.Server, ps *pathStream, desc *description.Session) *gortsplib.ServerStream {
	ps.mu.Lock(); defer ps.mu.Unlock()
	if ps.stream != nil { return ps.stream }
	st := &gortsplib.ServerStream{Server: server, Desc: desc}
	if err := st.Initialize(); err != nil { log.Fatalf("stream initialize: %v", err) }
	ps.stream = st; return st
}

func relayPath(server *gortsplib.Server, ps *pathStream, path string) {
	src := "rtsp://127.0.0.1:8554/" + path
	for { if err := relayOnce(server, ps, path, src); err != nil { log.Printf("relay %s: %v", path, err) }; time.Sleep(2*time.Second) }
}

func relayOnce(server *gortsplib.Server, ps *pathStream, path, src string) error {
	u, err := base.ParseURL(src); if err != nil { return err }
	c := &gortsplib.Client{Scheme: u.Scheme, Host: u.Host, ReadTimeout: 10*time.Second, WriteTimeout: 10*time.Second}
	if err = c.Start(); err != nil { return err }; defer c.Close()
	desc, _, err := c.Describe(u); if err != nil { return err }
	if err = c.SetupAll(desc.BaseURL, desc.Medias); err != nil { return err }
	st := setStream(server, ps, desc); log.Printf("relay ready path=%s from=%s", path, src)
	stableMedia := func(in *description.Media) *description.Media {
		for _, candidate := range st.Desc.Medias {
			if candidate.Type == in.Type && len(candidate.Formats) == len(in.Formats) && len(candidate.Formats) > 0 &&
				candidate.Formats[0].RTPMap() == in.Formats[0].RTPMap() && candidate.Formats[0].PayloadType() == in.Formats[0].PayloadType() { return candidate }
		}
		return nil
	}
	c.OnPacketRTPAny(func(media *description.Media, _ format.Format, pkt *rtp.Packet) {
		target := stableMedia(media); if target == nil { log.Printf("relay write %s: unknown media", path); return }
		if err := st.WritePacketRTP(target, pkt); err != nil { log.Printf("relay write %s: %v", path, err) }
	})
	if _, err = c.Play(nil); err != nil { return err }; return c.Wait()
}

func metadataXML() []byte {
	now := time.Now().UTC().Format(time.RFC3339Nano)
	return []byte(fmt.Sprintf(`<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema"><tt:VideoAnalytics><tt:Frame UtcTime="%s"><tt:Object ObjectId="1"><tt:Appearance><tt:Shape><tt:BoundingBox left="0.10" top="0.10" right="0.40" bottom="0.60"/></tt:Shape><tt:Class><tt:Type Likelihood="0.90">Human</tt:Type></tt:Class></tt:Appearance></tt:Object></tt:Frame></tt:VideoAnalytics></tt:MetadataStream>`, now))
}
func startMetadata(server *gortsplib.Server, ps *pathStream) {
	f := &format.Generic{PayloadTyp: 107, RTPMa: "vnd.onvif.metadata/90000", FMT: map[string]string{}}; if err := f.Init(); err != nil { log.Fatalf("metadata format: %v", err) }
	m := &description.Media{Type: description.MediaTypeApplication, Profile: headers.TransportProfileAVP, Control: "trackID=metadata", Formats: []format.Format{f}}
	st := setStream(server, ps, &description.Session{Title: "ONVIF metadata", Medias: []*description.Media{m}})
	go func() { ticker := time.NewTicker(500*time.Millisecond); defer ticker.Stop(); var seq uint32; start := time.Now(); for range ticker.C { pkt := &rtp.Packet{Header: rtp.Header{Version: 2, Marker: true, PayloadType: 107, SequenceNumber: uint16(atomic.AddUint32(&seq, 1)), Timestamp: uint32(time.Since(start).Seconds()*90000), SSRC: 0x4d455441}, Payload: metadataXML()}; if err := st.WritePacketRTP(m, pkt); err != nil { log.Printf("metadata write: %v", err) } } }()
	log.Printf("metadata ready path=metadata")
}

func main() {
	paths := map[string]*pathStream{"main": {}, "jpeg": {}, "sub1": {}, "sub2": {}, "metadata": {}}
	h := &handler{paths: paths}; srv := &gortsplib.Server{RTSPAddress: ":8555", UDPRTPAddress: ":8050", UDPRTCPAddress: ":8051", Handler: h, ReadTimeout: 10*time.Second, WriteTimeout: 10*time.Second, IdleTimeout: 60*time.Second}; if err := srv.Start(); err != nil { log.Fatalf("server start: %v", err) }
	startMetadata(srv, paths["metadata"]); for _, p := range []string{"main", "jpeg", "sub1", "sub2"} { go relayPath(srv, paths[p], p) }; log.Printf("RTSP tunnel relay listening on :8555"); if err := srv.Wait(); err != nil { log.Fatal(err) }
}
