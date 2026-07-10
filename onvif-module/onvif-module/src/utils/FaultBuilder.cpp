#include "utils/FaultBuilder.h"
#include <sstream>

std::string FaultBuilder::sender(const std::string& subcode1,
                                 const std::string& subcode2,
                                 const std::string& reason) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
          "<SOAP-ENV:Envelope"
          " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
          " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
          "<SOAP-ENV:Body><SOAP-ENV:Fault>"
          "<SOAP-ENV:Code>"
          "<SOAP-ENV:Value>SOAP-ENV:Sender</SOAP-ENV:Value>"
          "<SOAP-ENV:Subcode>"
          "<SOAP-ENV:Value>" << subcode1 << "</SOAP-ENV:Value>";
    if (!subcode2.empty()) {
        os << "<SOAP-ENV:Subcode>"
              "<SOAP-ENV:Value>" << subcode2 << "</SOAP-ENV:Value>"
              "</SOAP-ENV:Subcode>";
    }
    os << "</SOAP-ENV:Subcode>"
          "</SOAP-ENV:Code>"
          "<SOAP-ENV:Reason>"
          "<SOAP-ENV:Text xml:lang=\"en\">" << reason << "</SOAP-ENV:Text>"
          "</SOAP-ENV:Reason>"
          "</SOAP-ENV:Fault></SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

std::string FaultBuilder::receiver(const std::string& subcode1,
                                   const std::string& reason) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
          "<SOAP-ENV:Envelope"
          " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
          " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
          "<SOAP-ENV:Body><SOAP-ENV:Fault>"
          "<SOAP-ENV:Code>"
          "<SOAP-ENV:Value>SOAP-ENV:Receiver</SOAP-ENV:Value>"
          "<SOAP-ENV:Subcode>"
          "<SOAP-ENV:Value>" << subcode1 << "</SOAP-ENV:Value>"
          "</SOAP-ENV:Subcode>"
          "</SOAP-ENV:Code>"
          "<SOAP-ENV:Reason>"
          "<SOAP-ENV:Text xml:lang=\"en\">" << reason << "</SOAP-ENV:Text>"
          "</SOAP-ENV:Reason>"
          "</SOAP-ENV:Fault></SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

std::string FaultBuilder::notAuthorized() {
    return sender("ter:NotAuthorized", "", "Sender not authorized");
}

std::string FaultBuilder::actionNotSupported() {
    return sender("ter:ActionNotSupported", "", "Action not supported");
}

std::string FaultBuilder::invalidArgVal(const std::string& reason) {
    return sender("ter:InvalidArgVal", "ter:MalformedData", reason);
}

std::string FaultBuilder::noProfile() {
    return sender("ter:InvalidArgVal", "ter:NoProfile",
                  "No profile with the given token");
}
