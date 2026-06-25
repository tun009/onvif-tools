#!/bin/bash
# scripts/patch_generated.sh
GEN_DIR="$(cd "$(dirname "$0")/.." && pwd)/generated"

echo "[PATCH] Fixing generated files..."

# Fix: namespace map
NSMAP="$GEN_DIR/onvif.nsmap"
if [ ! -f "$NSMAP" ]; then
cat > "$NSMAP" << 'NSEOF'
struct Namespace namespaces[] = {
    {"SOAP-ENV","http://www.w3.org/2003/05/soap-envelope","http://schemas.xmlsoap.org/soap/envelope/",NULL},
    {"SOAP-ENC","http://www.w3.org/2003/05/soap-encoding","http://schemas.xmlsoap.org/soap/encoding/",NULL},
    {"xsi","http://www.w3.org/2001/XMLSchema-instance","http://www.w3.org/1999/XMLSchema-instance",NULL},
    {"xsd","http://www.w3.org/2001/XMLSchema",NULL,NULL},
    {"wsa5","http://www.w3.org/2005/08/addressing",NULL,NULL},
    {"wsse","http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd",NULL,NULL},
    {"wsu","http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd",NULL,NULL},
    {"wsdd","http://docs.oasis-open.org/ws-dd/ns/discovery/2009/01",NULL,NULL},
    {"tt","http://www.onvif.org/ver10/schema",NULL,NULL},
    {"tds","http://www.onvif.org/ver10/device/wsdl",NULL,NULL},
    {"tr2","http://www.onvif.org/ver20/media/wsdl",NULL,NULL},
    {"tptz","http://www.onvif.org/ver20/ptz/wsdl",NULL,NULL},
    {"timg","http://www.onvif.org/ver20/imaging/wsdl",NULL,NULL},
    {"tev","http://www.onvif.org/ver10/events/wsdl",NULL,NULL},
    {"tan","http://www.onvif.org/ver20/analytics/wsdl",NULL,NULL},
    {"tdn","http://www.onvif.org/ver10/network/wsdl",NULL,NULL},
    {"wsnt","http://docs.oasis-open.org/wsn/b-2",NULL,NULL},
    {NULL,NULL,NULL,NULL}
};
NSEOF
    echo "  [CREATED] onvif.nsmap"
fi

echo "[PATCH] Done."
