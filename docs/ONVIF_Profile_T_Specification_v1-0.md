ONVIF Profile T Specification v1.0 

## **ONVIF**[®] 

## **Profile T Specification** 

Version 1.0 

September 2018 

www.onvif.org 

1 

ONVIF Profile T Specification v1.0 

©2008-2018 by ONVIF: Open Network Video Interface Forum. All rights reserved. 

Recipients of this document may copy, distribute, publish, or display this document so long as this copyright notice, license and disclaimer are retained with all copies of the document. No license is granted to modify this document. 

THIS DOCUMENT IS PROVIDED "AS IS," AND THE CORPORATION AND ITS MEMBERS AND THEIR AFFILIATES, MAKE NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO, WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT, OR TITLE; THAT THE CONTENTS OF THIS DOCUMENT ARE SUITABLE FOR ANY PURPOSE; OR THAT THE IMPLEMENTATION OF SUCH CONTENTS WILL NOT INFRINGE ANY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. 

IN NO EVENT WILL THE CORPORATION OR ITS MEMBERS OR THEIR AFFILIATES BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL, PUNITIVE OR CONSEQUENTIAL DAMAGES, ARISING OUT OF OR RELATING TO ANY USE OR DISTRIBUTION OF THIS DOCUMENT, WHETHER OR NOT (1) THE CORPORATION, MEMBERS OR THEIR AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES, OR (2) SUCH DAMAGES WERE REASONABLY FORESEEABLE, AND ARISING OUT OF OR RELATING TO ANY USE OR DISTRIBUTION OF THIS DOCUMENT.  THE FOREGOING DISCLAIMER AND LIMITATION ON LIABILITY DO NOT APPLY TO, INVALIDATE, OR LIMIT REPRESENTATIONS AND WARRANTIES MADE BY THE MEMBERS AND THEIR RESPECTIVE AFFILIATES TO THE CORPORATION AND OTHER MEMBERS IN CERTAIN WRITTEN POLICIES OF THE CORPORATION. 

www.onvif.org 

2 

ONVIF Profile T Specification v1.0 

## REVISION HISTORY 

|**Vers. Date**|**Vers. Date**|**Description**|**Contributors**|
|---|---|---|---|
|1.0|September|Original release version 1.0|Refer to Contributors table|
||2018|||



www.onvif.org 

3 

ONVIF Profile T Specification v1.0 

## CONTRIBUTORS 

|CONTRIBUTORS||
|---|---|
|**Company**|**Contributors**|
|Axis Communications AB|Fredrik Svensson – Working Group chairman|
|Pelco by Schneider Electric|Andrew Downs – editor<br>Steve Wolf|
|Anixter|Bob Dolan|
|Avigilon Corporation|Travis Gredley|
|Bosch Security Systems|Hans Busch|
|Canon Inc.|Sriram Prasad Bhetanabottla<br>Raghavendra Shekaraiah|
|Genetec Inc.|Nicolas Brochu<br>Hugo Brisson|
|Hanwha Techwin|Sungbong Cho<br>Yogavanan Mathivanan<br>Sujith Raman|
|Oncam|Steven Dillingham|
|Panasonic System Networks Co., LTD|Hasan Timucin Ozdemir|
|Sony Corporation|Hiroyuki Sano|
|Videotec|Enrico Campana<br>Ottavio Campana|



www.onvif.org 

4 

ONVIF Profile T Specification v1.0 

## **Table of Contents** 

|**1**|**SCOPE.................................................................................................................................................... 7**|**SCOPE.................................................................................................................................................... 7**|
|---|---|---|
|**2**|**NORMATIVE REFERENCES ...................................................................................................................... 8**||
||2.1|NORMATIVEREFERENCES.............................................................................................................................. 8|
|**3**|**TERMS AND DEFINITIONS ...................................................................................................................... 9**||
||3.1|DEFINITIONS............................................................................................................................................... 9|
|**4**|**TECHNICAL SPECIFICATION VERSION REQUIREMENT ............................................................................ 10**||
|**5**|**REQUIREMENT LEVELS ......................................................................................................................... 11**||
|**6**|**OVERVIEW ........................................................................................................................................... 12**||
|**7**|**PROFILE MANDATORY FEATURES (NORMATIVE) ................................................................................... 13**||
||7.1|USER AUTHENTICATION............................................................................................................................... 14|
||7.2|CAPABILITIES............................................................................................................................................. 15|
||7.3|DISCOVERY............................................................................................................................................... 17|
||7.4|NETWORKCONFIGURATION......................................................................................................................... 19|
||7.5|SYSTEM................................................................................................................................................... 21|
||7.6|USERHANDLING....................................................................................................................................... 22|
||7.7|EVENTHANDLING...................................................................................................................................... 23|
||7.8|MEDIAPROFILEMANAGEMENT................................................................................................................... 25|
||7.9|VIDEOSTREAMING.................................................................................................................................... 27|
||7.10|CONFIGURATION OFVIDEOPROFILE.............................................................................................................. 30|
||7.11|VIDEOSOURCECONFIGURATION.................................................................................................................. 32|
||7.12|VIDEOENCODERCONFIGURATION................................................................................................................ 34|
||7.13|METADATASTREAMING.............................................................................................................................. 35|
||7.14|CONFIGURATION OFMETADATAPROFILE........................................................................................................ 37|
||7.15|METADATACONFIGURATION........................................................................................................................ 39|
||7.16|IMAGINGSETTINGS.................................................................................................................................... 40|
||7.17|TAMPERING............................................................................................................................................. 41|
||7.18|CONFIGURATION OFON-SCREENDISPLAY(OSD) ............................................................................................ 42|
||7.19|JPEG SNAPSHOT....................................................................................................................................... 44|
||7.20|MOTIONALARMEVENTS............................................................................................................................ 45|
||7.21|ABSOLUTEPTZ MOVE................................................................................................................................ 46|
||7.22|CONTINUOUSPTZ MOVE........................................................................................................................... 48|
|**8**|**PROFILE CONDITIONAL FEATURES (NORMATIVE) .................................................................................. 50**||
||8.1|CONFIGURATION OFPTZ PROFILE................................................................................................................. 51|
||8.2|PTZ CONFIGURATION................................................................................................................................. 53|
||8.3|PTZ PRESETS............................................................................................................................................ 54|
||8.4|PTZ HOMEPOSITION................................................................................................................................. 56|
||8.5|CONFIGURATION OFANALYTICSPROFILE........................................................................................................ 57|



www.onvif.org 

5 

ONVIF Profile T Specification v1.0 

|8.6|MOTIONREGIONDETECTORCONFIGURATION................................................................................................. 59|
|---|---|
|8.7|VIDEOSOURCEMODE................................................................................................................................ 61|
|8.8|NTP ....................................................................................................................................................... 62|
|8.9|AUDIOSTREAMING.................................................................................................................................... 63|
|8.10|CONFIGURATION OFAUDIOPROFILE............................................................................................................. 65|
|8.11|AUDIOENCODERCONFIGURATION................................................................................................................ 67|
|8.12|AUDIOOUTPUTSTREAMING........................................................................................................................ 68|
|8.13|CONFIGURATION OFAUDIOOUTPUTPROFILE................................................................................................. 70|
|8.14|FOCUSCONTROL....................................................................................................................................... 72|
|8.15|RELAYOUTPUTS........................................................................................................................................ 73|
|8.16|DIGITALINPUTS......................................................................................................................................... 75|
|8.17|AUXILIARYCOMMANDS.............................................................................................................................. 76|



www.onvif.org 

6 

ONVIF Profile T Specification v1.0 

## **1 Scope** 

This document defines the mandatory and conditional features required by an ONVIF device and ONVIF client that support Profile T. 

www.onvif.org 

7 

ONVIF Profile T Specification v1.0 

## **2 Normative references** 

This section defines the normative references applicable to this specification. 

## 2.1 Normative references 

- **IANA Media Type Reference** 

< http://www.iana.org/assignments/media-types/media-types.xhtml **>** 

- **ONVIF Profile Policy** 

< http://www.onvif.org/profiles> 

- **ONVIF Network Interface Specifications** 

< https://www.onvif.org/profiles/specifications/ > 

www.onvif.org 

8 

ONVIF Profile T Specification v1.0 

## **3 Terms and definitions** 

This section provides common terms and definitions used in this specification. 

## 3.1 Definitions 

**profile** 

See [ONVIF Profile Policy] 

**ONVIF device** 

**ONVIF client** 

**tns1** 

Networked hardware appliance or software program that exposes one or multiple ONVIF Web Services 

Networked hardware appliance or software program that uses ONVIF Web Services. 

A prefix for the ONVIF topic namespace "http://www.onvif.org/ver10/topics". This prefix is not part of the standard and an implementation can use any prefix. See [ONVIF Network Interface Specifications] Core Specification description of Namespaces for details. 

www.onvif.org 

9 

ONVIF Profile T Specification v1.0 

## **4 Technical specification version requirement** 

Implementation of ONVIF Network Interface Specifications, version 18.06 or later is required for conformance to Profile T. 

www.onvif.org 

10 

ONVIF Profile T Specification v1.0 

## **5 Requirement levels** 

Each feature in this document has a requirement level for device and client that claim conformance to Profile T and contains a Function List that states the functions requirement level for device and client that implement that feature. 

The requirement levels for features are: 

- **Mandatory = Feature that shall be implemented by an ONVIF device or ONVIF client.** 

- **Conditional = Feature that shall be implemented by an ONVIF device or ONVIF client if it supports that functionality in any way, including any proprietary way. Features that are conditional are marked with “if supported” in a profile specification.** 

The requirement levels for functions are: 

- **Mandatory = Function that shall be implemented by an ONVIF device or ONVIF client.** 

- **Conditional = Function that shall be implemented by an ONVIF device or ONVIF client if it supports that functionality.** 

- **Optional = Function that may be implemented by an ONVIF device or ONVIF client.** 

Function Lists use the following abbreviations: 

- **M = Mandatory** 

- **C = Conditional** 

- **O = Optional** 

All functions shall be implemented as described in the corresponding [ONVIF Network Interface Specifications]. 

www.onvif.org 

11 

ONVIF Profile T Specification v1.0 

## **6 Overview** 

An ONVIF profile is described by a fixed set of functionalities through a number of services that are provided by the ONVIF standard. A number of services and functionalities are mandatory for each type of ONVIF profile. An ONVIF device and client may support any combination of profiles and other optional services and functionalities. 

An ONVIF device conformant with Profile T is an ONVIF device that sends video data over an IP network to a client. Profile T also includes support for a number of features, including but not limited to: imaging, metadata streaming, onscreen display, and motion alarm events. Other features that may be supported on the device include PTZ, analytics, motion region configuration, bidirectional audio, digital inputs, and relay outputs. For example, a device conformant with Profile T may be an IP network camera or an encoder device. 

An ONVIF client conformant with Profile T is an ONVIF client that can configure, request, and control streaming of video data over an IP network from an ONVIF device conformant with Profile T. Profile T also includes support for control of a number of features, including but not limited to imaging and motion alarm events. Other features that may be supported by the client include metadata streaming, onscreen display, PTZ, analytics, motion region configuration, bidirectional audio, digital inputs, and relay outputs. 

www.onvif.org 

12 

ONVIF Profile T Specification v1.0 

## **7 Profile mandatory features (normative)** 

Devices and clients conformant to Profile T shall support the following features. The requirements represent the minimum functionality that must be implemented for conformance. 

www.onvif.org 

13 

ONVIF Profile T Specification v1.0 

## 7.1 User authentication 

This section describes the required method of user authentication. 

## 7.1.1 Device requirements 

- Device shall authenticate HTTP requests using Digest authentication as described by the **Core Specification** . 

- Device shall authenticate RTSP requests using Digest authentication as described by the **Core Specification** . 

- Device shall authenticate RTSP requests tunneled over HTTP using Digest authentication on the RTSP level as described by the **Core Specification** . 

## 7.1.2 Client requirements 

- Client shall support authenticating HTTP requests using Digest authentication as described by the **Core Specification** . 

- Client shall support authenticating RTSP requests using Digest authentication as described by the **Core Specification** . 

- Client shall support authenticating RTSP requests tunneled over HTTP using Digest authentication on the RTSP level as described by the **Core Specification** . 

## 7.1.3 Function list for devices 

|**User Authentication**<br>**Device MANDATORY**|**User Authentication**<br>**Device MANDATORY**|**User Authentication**<br>**Device MANDATORY**|**User Authentication**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||Digest authentication|Core|M|



## 7.1.4 Function list for clients 

|**User Authentication**<br>**Client MANDATORY**|**User Authentication**<br>**Client MANDATORY**|**User Authentication**<br>**Client MANDATORY**|**User Authentication**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||Digest authentication|Core|M|



www.onvif.org 

14 

ONVIF Profile T Specification v1.0 

## 7.2 Capabilities 

This section describes the operations related to obtaining the capabilities of a device. 

## 7.2.1 Device requirements 

- Device shall support **GetServices** and **GetServiceCapabilities** as detailed in the **Core Specification** . 

- Device shall support **GetServiceCapabilities** as detailed in the **Media 2** , **Imaging** , and **Device IO Service Specifications** . 

- If supported, device shall support **GetServiceCapabilities** as detailed in the **Analytics** and **PTZ Service Specifications** . 

- Device shall provide the WSDL URL in response to the **GetWsdlUrl** operation. 

- Device shall indicate support for at least two pull point subscriptions by returning **MaxPullPoints** set to no less than two in the response to **GetServiceCapabilities** in the event service. 

- Device shall return its capabilities for the maximum number of profiles ( **MaximumNumberOfProfiles** ) in the **GetServiceCapabilities** response of the Media 2 service. 

## 7.2.2 Client requirements 

- Client shall determine the available **Services** using the **GetServices** operation. 

## 7.2.3 Function list for devices 

|**Capabilities**<br>**Device MANDATORY**|**Capabilities**<br>**Device MANDATORY**|**Capabilities**<br>**Device MANDATORY**|**Capabilities**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetServices|Device Management|M|
||GetServiceCapabilities|Device Management|M|
||GetWsdlUrl|Device Management|M|
||GetServiceCapabilities|Event|M|
||GetServiceCapabilities|Media 2|M|
||GetServiceCapabilities|Imaging|M|
||GetServiceCapabilities|PTZ|C|
||GetServiceCapabilities|DeviceIO|M|
||GetServiceCapabilities|Analytics|C|



www.onvif.org 

15 

ONVIF Profile T Specification v1.0 

## 7.2.4 Function list for clients 

|**Capabilities**<br>**Client MANDATORY**|**Capabilities**<br>**Client MANDATORY**|**Capabilities**<br>**Client MANDATORY**|**Capabilities**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetServices|Device Management|M|
||GetServiceCapabilities|Device Management|O|
||GetWsdlUrl|Device Management|O|
||GetServiceCapabilities|Event|O|
||GetServiceCapabilities|Media 2|O|
||GetServiceCapabilities|Imaging|O|
||GetServiceCapabilities|PTZ|O|
||GetServiceCapabilities|DeviceIO|O|
||GetServiceCapabilities|Analytics|O|



www.onvif.org 

16 

ONVIF Profile T Specification v1.0 

## 7.3 Discovery 

This section describes the operations related to device discovery. 

## 7.3.1 Device requirements 

- Device shall support **WS-Discovery** as specified in the **Core Specification** . 

- Device shall support discovery mode using the operations **GetDiscoveryMode** and **SetDiscoveryMode** . 

- Device shall support listing, adding, modifying and removing discovery scopes using the operations **GetScopes** , **AddScopes** , **SetScopes** and **RemoveScopes** 

- Device shall support the Profile T-specific scope parameter presented in 7.3.5 **Scope Parameters** . 

## 7.3.2 Client requirements 

- Client shall be able to discover a device using **WS-Discovery** as specified in the **Core Specification** . 

## 7.3.3 Function list for devices 

|**Discovery**<br>**Device MANDATORY**|**Discovery**<br>**Device MANDATORY**|**Discovery**<br>**Device MANDATORY**|**Discovery**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||WS-Discovery|Core|M|
||GetDiscoveryMode|Device Management|M|
||SetDiscoveryMode|Device Management|M|
||GetScopes|Device Management|M|
||SetScopes|Device Management|M|
||AddScopes|Device Management|M|
||RemoveScopes|Device Management|M|



www.onvif.org 

17 

ONVIF Profile T Specification v1.0 

## 7.3.4 Function list for clients 

|**Discovery**<br>**Client MANDATORY**|**Discovery**<br>**Client MANDATORY**|**Discovery**<br>**Client MANDATORY**|**Discovery**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||WS-Discovery|Core|M|
||GetDiscoveryMode|Device Management|O|
||SetDiscoveryMode|Device Management|O|
||GetScopes|Device Management|O|
||SetScopes|Device Management|O|
||AddScopes|Device Management|O|
||RemoveScopes|Device Management|O|



## 7.3.5 Scope parameters 

|**Discovery**|**Discovery**|**Discovery**|**Discovery**|
|---|---|---|---|
||**Category**|**Defined Values**|**Description**|
||Profile|T|The scope indicates if the device is conformant with<br>Profile T. A device conformant with Profile T shall<br>include a scope entrywith this value in its scope list.|



www.onvif.org 

18 

ONVIF Profile T Specification v1.0 

## 7.4 Network configuration 

This section describes the operations related to the configuration of network settings on the device. 

## 7.4.1 Device requirements 

- Device shall support listing and configuring the device hostname using the **GetHostName** and **SetHostName** operations. 

- Device shall support listing and configuring the DNS values using the **GetDNS** and **SetDNS** operations. 

- Device shall support listing and configuring supported network interfaces on the device using the **GetNetworkInterfaces** and **SetNetworkInterfaces** operations. 

- Device shall support listing and configuring supported network protocols on the device using the **GetNetworkProtocols** and **SetNetworkProtocols** operations. 

- Device shall support listing and configuring the default gateway of the device using the **GetNetworkDefaultGateway** and **SetNetworkDefaultGateway** operations. 

## 7.4.2 Client requirements 

- Client shall be able to list and configure supported network interfaces on the device using the **GetNetworkInterfaces** and **SetNetworkInterfaces** operations. 

- Client shall be able to list and set the default gateway of the device using the **GetNetworkDefaultGateway** and **SetNetworkDefaultGateway** operations. 

## 7.4.3 Function list for devices 

|**Network Configuration**<br>**Device MANDATORY**|**Network Configuration**<br>**Device MANDATORY**|**Network Configuration**<br>**Device MANDATORY**|**Network Configuration**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetHostName|Device Management|M|
||SetHostName|Device Management|M|
||GetDNS|Device Management|M|
||SetDNS|Device Management|M|
||GetNetworkInterfaces|Device Management|M|
||SetNetworkInterfaces|Device Management|M|
||GetNetworkProtocols|Device Management|M|
||SetNetworkProtocols|Device Management|M|
||GetNetworkDefaultGateway|Device Management|M|
||SetNetworkDefaultGateway|Device Management|M|



www.onvif.org 

19 

ONVIF Profile T Specification v1.0 

## 7.4.4 Function list for clients 

|**Network Configuration**<br>**Client MANDATORY**|**Network Configuration**<br>**Client MANDATORY**|**Network Configuration**<br>**Client MANDATORY**|**Network Configuration**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetHostName|Device Management|O|
||SetHostName|Device Management|O|
||GetDNS|Device Management|O|
||SetDNS|Device Management|O|
||GetNetworkInterfaces|Device Management|M|
||SetNetworkInterfaces|Device Management|M|
||GetNetworkProtocols|Device Management|O|
||SetNetworkProtocols|Device Management|O|
||GetNetworkDefaultGateway|Device Management|M|
||SetNetworkDefaultGateway|Device Management|M|



www.onvif.org 

20 

ONVIF Profile T Specification v1.0 

## 7.5 System 

This section describes the operations related to obtaining device information and the configuration of device settings. 

## 7.5.1 Device requirements 

- Device shall support the listing of device information such as manufacturer, model and firmware version using the **GetDeviceInformation** operation. 

- Device shall support listing and configuring the date and time on the device using the **GetSystemDateAndTime** and **SetSystemDateAndTime** operations. 

- Device shall support the ability to return to factory settings using the **SetSystemFactoryDefault** operation. 

- Device shall support rebooting using the **SystemReboot** operation. 

## 7.5.2 Client requirements (if supported) 

- Client shall be able to get device information such as manufacturer, model and firmware version using the **GetDeviceInformation** operation. 

## 7.5.3 Function list for devices 

|**System**<br>**Device MANDATORY**|**System**<br>**Device MANDATORY**|**System**<br>**Device MANDATORY**|**System**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetDeviceInformation|Device Management|M|
||GetSystemDateAndTime|Device Management|M|
||SetSystemDateAndTime|Device Management|M|
||SetSystemFactoryDefault|Device Management|M|
||SystemReboot|Device Management|M|



## 7.5.4 Function list for clients 

|**System**<br>**Client CONDITIONAL**|**System**<br>**Client CONDITIONAL**|**System**<br>**Client CONDITIONAL**|**System**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetDeviceInformation|Device Management|M|
||GetSystemDateAndTime|Device Management|O|
||SetSystemDateAndTime|Device Management|O|
||SetSystemFactoryDefault|Device Management|O|
||SystemReboot|Device Management|O|



www.onvif.org 

21 

ONVIF Profile T Specification v1.0 

## 7.6 User handling 

This section describes the operations related to managing users on the device. 

## 7.6.1 Device requirements 

- Device shall support creating, listing, modifying and deleting users from the device using the **CreateUsers** , **GetUsers** , **SetUser** and **DeleteUsers** operations. 

## 7.6.2 Client requirements (if supported) 

- Client shall be able to create, list, modify and delete users from the device using the **CreateUsers** , **GetUsers** , **SetUser** and **DeleteUsers** operations. 

## 7.6.3 Function list for devices 

|**User Handling**<br>**Device MANDATORY**|**User Handling**<br>**Device MANDATORY**|**User Handling**<br>**Device MANDATORY**|**User Handling**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetUsers|Device Management|M|
||CreateUsers|Device Management|M|
||DeleteUsers|Device Management|M|
||SetUser|Device Management|M|



## 7.6.4 Function list for clients 

|**User Handling**<br>**Client CONDITIONAL**|**User Handling**<br>**Client CONDITIONAL**|**User Handling**<br>**Client CONDITIONAL**|**User Handling**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetUsers|Device Management|M|
||CreateUsers|Device Management|M|
||DeleteUsers|Device Management|M|
||SetUser|Device Management|M|



www.onvif.org 

22 

ONVIF Profile T Specification v1.0 

## 7.7 Event handling 

This section describes the operations related to retrieving and filtering events. The Real-time Pull-Point Notification Interface as covered by the **Core Specification** is Mandatory for Profile T conformance. 

## 7.7.1 Device requirements 

- Device shall support event handling with a pull point using the **SetSynchronizationPoint** , **CreatePullPointSubscription** and **PullMessage** operations. 

- Device shall support retrieval of supported filter dialects and topics using the **GetEventProperties** operation. 

- Device shall support event filtering using **Message Content Filter** and **Topic Filter** as described in the **Core Specification** . 

- Device shall return the following **MessageContentFilterDialect** in response to **GetEventProperties** : 

   - http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter 

- Device shall support subscription management using the **Unsubscribe** operation. 

- Device shall support at least two concurrent pull point subscriptions. 

## 7.7.2 Client requirements 

- Client shall implement event handling with a pull point using the **SetSynchronizationPoint** , **CreatePullPointSubscription** and **PullMessage** operations. 

## 7.7.3 Function list for devices 

|**Event Handling**<br>**Device MANDATORY**|**Event Handling**<br>**Device MANDATORY**|**Event Handling**<br>**Device MANDATORY**|**Event Handling**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||SetSynchronizationPoint|Event|M|
||CreatePullPointSubscription|Event|M|
||PullMessages|Event|M|
||GetEventProperties|Event|M|
||Unsubscribe|Event|M|
||Filterparameter of CreatePullPointSubscriptionRequest|Event|M|
||MessageContentFilterDialect<br>http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter|Event|M|



www.onvif.org 

23 

ONVIF Profile T Specification v1.0 

## 7.7.4 Function list for clients 

|**Event Handling**<br>**Client MANDATORY**|**Event Handling**<br>**Client MANDATORY**|**Event Handling**<br>**Client MANDATORY**|**Event Handling**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||SetSynchronizationPoint|Event|M|
||CreatePullPointSubscription|Event|M|
||PullMessages|Event|M|
||GetEventProperties|Event|O|
||Unsubscribe|Event|O|
||Filterparameter of CreatePullPointSubscriptionRequest|Event|O|
||MessageContentFilterDialect<br>http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter|Event|C*|



*Client shall support this dialect if Message Content Filter is supported. 

www.onvif.org 

24 

ONVIF Profile T Specification v1.0 

## 7.8 Media profile management 

This section describes the operations related to the creation and deletion of **Media Profiles** . 

## 7.8.1 Device requirements 

- Device shall support creation of **Media Profiles** using the **CreateProfile** operation, containing at least one of the configuration types **Video Source Configuration** , **Audio Source Configuration** or **Audio Output Configuration** . 

- Device shall support deletion of **Media Profiles** using the **DeleteProfile** operation. 

- Device shall return its capabilities for the maximum number of concurrent streams in the **GetVideoEncoderInstances** response. 

- For each **Video Source Configuration** returned by **GetVideoSourceConfigurations** the device shall support creation of a minimum of as many **Media Profiles** as instances returned by **GetVideoEncoderInstances** for that video source configuration token. 

- Device shall deliver event notifications when a **Media Profile** is created or deleted. 

## 7.8.2 Client requirements (if supported) 

- Client shall be able to create **Media Profiles** using the **CreateProfile** operation, containing at least one of the configuration types **Video Source Configuration** , **Audio Source Configuration** or **Audio Output Configuration** . 

- Client shall be able to query the maximum number of concurrent streams using the **GetVideoSourceConfigurations** and **GetVideoEncoderInstances** operations. 

## 7.8.3 Function list for devices 

|**Media Profile Management**<br>**Device MANDATORY**|**Media Profile Management**<br>**Device MANDATORY**|**Media Profile Management**<br>**Device MANDATORY**|**Media Profile Management**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||CreateProfile|Media 2|M|
||DeleteProfile|Media 2|M|
||GetVideoSourceConfigurations|Media 2|M|
||GetVideoEncoderInstances|Media 2|M|
||tns1:Media/ProfileChanged|Event|M|



www.onvif.org 

25 

ONVIF Profile T Specification v1.0 

## 7.8.4 Function list for clients 

|**Media Profile Management**<br>**Client CONDITIONAL**|**Media Profile Management**<br>**Client CONDITIONAL**|**Media Profile Management**<br>**Client CONDITIONAL**|**Media Profile Management**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||CreateProfile|Media 2|M|
||DeleteProfile|Media 2|O|
||GetVideoSourceConfigurations|Media 2|M|
||GetVideoEncoderInstances|Media 2|M|
||tns1:Media/ProfileChanged|Event|O|



www.onvif.org 

26 

ONVIF Profile T Specification v1.0 

## 7.9 Video streaming 

This section describes the operations related to the setup and control of video streaming. 

## 7.9.1 Device requirements 

- Device shall provide at least one ready-to-use **Media Profile** for streaming H.264 or H.265 video per video source. 

- Device shall support listing of **Media Profiles** in response to the **GetProfiles** operation. 

- Device shall return the stream URI in response to the **GetStreamUri** operation. 

- Device shall support streaming of at least one of the H.264 and H.265 encoding formats 

- Device shall support initiation of streaming sessions using RTSP according to the **Streaming Service Specification** . 

- Device shall be able to stream video over RTP/UDP using the selected **Media Profile** . 

- Device shall be able to stream video over RTP/RTSP/HTTP/TCP using the selected **Media Profile** . 

- If supported, device shall be able to stream video over RTP/RTSP/HTTPS/TCP using the selected **Media Profile** . 

- Device shall be able to stream video over RTP/UDP multicast using the selected **Media Profile** . 

- If supported, device shall be able to stream video over RTP/RTSP/TCP/WebSocket using the selected **Media Profile** . 

- Device shall send a key frame on-demand upon reception of the **SetSynchronizationPoint** operation when streaming H.264 or H.265. 

## 7.9.2 Client requirements 

- Client shall be able to request the stream URI for the selected **Media Profile** using the **GetProfiles** and **GetStreamURI** operations. 

- Client shall be able to initiate streaming sessions using RTSP according to the **Streaming Service Specification** . 

- Client shall be able to receive a stream and decode H.264 video using the selected **Media Profile** . 

- Client shall be able to receive a stream and decode H.265 video using the selected **Media Profile** . 

www.onvif.org 

27 

ONVIF Profile T Specification v1.0 

- Client shall be able to receive a video stream over RTP/UDP or RTP/RTSP/HTTP/TCP using the selected **Media Profile** . 

- If supported, client shall be able to receive a video stream over RTP/RTSP/HTTPS/TCP using the selected **Media Profile** . 

- If supported, client shall be able to receive a video stream over RTP/UDP multicast using the selected **Media Profile** . 

## 7.9.3 Function list for devices 

|**Video Streaming**<br>**Device MANDATORY**|**Video Streaming**<br>**Device MANDATORY**|**Video Streaming**<br>**Device MANDATORY**|**Video Streaming**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetStreamUri|Media 2|M|
||Video StreamingusingRTSP|Streaming|M|
||H.264 Encoding|Media 2|M*|
||H.265 Encoding|Media 2||
||Streamingover RTP/UDP|Streaming|M|
||Streamingover RTP/RTSP/HTTP/TCP|Streaming|M|
||Streamingover RTP/RTSP/HTTPS/TCP|Streaming|C|
||Streamingover RTP/UDP Multicast|Streaming|M|
||Streamingover RTP/RTSP/TCP/WebSocket|Streaming|C|
||SetSynchronizationPoint|Media 2|M|



* Device shall support at least one of the listed encoding formats. H.264 and H.265 are conditionally required. 

www.onvif.org 

28 

ONVIF Profile T Specification v1.0 

## 7.9.4 Function list for clients 

|**Video Streaming**<br>**Client MANDATORY**|**Video Streaming**<br>**Client MANDATORY**|**Video Streaming**<br>**Client MANDATORY**|**Video Streaming**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetStreamUri|Media 2|M|
||Video StreamingusingRTSP|Streaming|M|
||H.264 Decoding|Media 2|M|
||H.265 Decoding|Media 2|M|
||Streamingover RTP/UDP|Streaming|M*|
||Streamingover RTP/RTSP/HTTP/TCP|Streaming||
||Streamingover RTP/RTSP/HTTPS/TCP|Streaming|C|
||Streamingover RTP/UDP Multicast|Streaming|C|
||Streamingover RTP/RTSP/TCP/WebSocket|Streaming|O|
||SetSynchronizationPoint|Media 2|O|



* Client shall support at least one of the listed transport methods. 

www.onvif.org 

29 

ONVIF Profile T Specification v1.0 

## 7.10 Configuration of video profile 

This section describes the operations related to the configuration of **Media Profiles** for video streaming. 

## 7.10.1 Device requirements 

- Device shall support listing of **Media Profiles** in response to the **GetProfiles** operation. 

- Device shall support listing of video sources in response to the **GetVideoSources** operation. 

- Device shall support adding a **Video Source Configuration** to a **Media Profile** using the **GetVideoSourceConfigurations** and **AddConfiguration** operations. 

- Device shall support adding a **Video Encoder Configuration** to a **Media Profile** using the **GetVideoEncoderConfigurations** and **AddConfiguration** operations. 

- Device shall support removing a **Video Source Configuration** or a **Video Encoder Configuration** from a profile using the **RemoveConfiguration** operation. 

- Device shall deliver event notifications when a **Video Source Configuration** or **Video Encoder Configuration** is added or removed from a **Media Profile** . 

## 7.10.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Media Profiles** using the **GetProfiles** operation. 

- Client shall be able to add a **Video Encoder Configuration** to a **Media Profile** using the **GetVideoEncoderConfigurations** and **AddConfiguration** operations. 

## 7.10.3 Function list for devices 

|**Configuration of Video Profile**<br>**Device MANDATORY**|**Configuration of Video Profile**<br>**Device MANDATORY**|**Configuration of Video Profile**<br>**Device MANDATORY**|**Configuration of Video Profile**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetVideoSources|DeviceIO|M|
||GetVideoSourceConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||GetVideoEncoderConfigurations|Media 2|M|
||RemoveConfiguration|Media 2|M|
||tns1:Media/ProfileChanged|Event|M|



www.onvif.org 

30 

ONVIF Profile T Specification v1.0 

## 7.10.4 Function list for clients 

|**Configuration of Video Profile**<br>**Client CONDITIONAL**|**Configuration of Video Profile**<br>**Client CONDITIONAL**|**Configuration of Video Profile**<br>**Client CONDITIONAL**|**Configuration of Video Profile**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetVideoSources|DeviceIO|O|
||GetVideoSourceConfigurations|Media 2|O|
||AddConfiguration|Media 2|M|
||GetVideoEncoderConfigurations|Media 2|M|
||RemoveConfiguration|Media 2|O|
||tns1:Media/ProfileChanged|Event|O|



www.onvif.org 

31 

ONVIF Profile T Specification v1.0 

## 7.11 Video source configuration 

This section describes the operations related to the listing and modification of video source configurations on the device. 

## 7.11.1 Device requirements 

- Device shall support listing of **Video Source Configurations** using the **GetVideoSourceConfigurations** operation. 

- For each **Video Source Configuration** , device shall return the list of options in response to the **GetVideoSourceConfigurationOptions** operation. 

- Device shall support setting the current **Video Source Configuration** using the **SetVideoSourceConfiguration** operation. 

- Device shall deliver event notifications when a **Video Source Configuration** is changed. 

## 7.11.2 Client requirements (if supported) 

- Client shall be able to retrieve the current **Video Source Configurations** using the **GetVideoSourceConfigurations** operation. 

- Client shall be able to modify a **Video Source Configuration** using the **GetVideoSourceConfigurationOptions** and **SetVideoSourceConfiguration** operations. 

## 7.11.3 Function list for devices 

|**Video Source Configuration**<br>**Device MANDATORY**|**Video Source Configuration**<br>**Device MANDATORY**|**Video Source Configuration**<br>**Device MANDATORY**|**Video Source Configuration**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoSourceConfigurations|Media 2|M|
||GetVideoSourceConfigurationOptions|Media 2|M|
||SetVideoSourceConfiguration|Media 2|M|
||tns1:Media/ConfigurationChanged|Event|M|



www.onvif.org 

32 

ONVIF Profile T Specification v1.0 

## 7.11.4 Function list for clients 

|**Video Source Configuration**<br>**Client CONDITIONAL**|**Video Source Configuration**<br>**Client CONDITIONAL**|**Video Source Configuration**<br>**Client CONDITIONAL**|**Video Source Configuration**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoSourceConfigurations|Media 2|M|
||GetVideoSourceConfigurationOptions|Media 2|M|
||SetVideoSourceConfiguration|Media 2|M|
||tns1:Media/ConfigurationChanged|Event|O|



www.onvif.org 

33 

ONVIF Profile T Specification v1.0 

## 7.12 Video encoder configuration 

This section describes the operations related to the listing and modification of video encoder configurations on the device. 

## 7.12.1 Device requirements 

- Device shall support listing of **Video Encoder Configurations** using the **GetVideoEncoderConfigurations** operation. 

- For each **Video Encoder Configuration** , device shall return the list of options in response to the **GetVideoEncoderConfigurationOptions** operation. 

- Device shall support setting the current **Video Encoder Configuration** using the **SetVideoEncoderConfiguration** operation. 

- Device shall deliver event notifications when a **Video Encoder Configuration** is changed. 

## 7.12.2 Client requirements 

- Client shall be able to modify a **Video Encoder Configuration** using the **GetVideoEncoderConfigurationOptions** and **SetVideoEncoderConfiguration** operations. 

## 7.12.3 Function list for devices 

|**Video Encoder Configuration**<br>**Device MANDATORY**|**Video Encoder Configuration**<br>**Device MANDATORY**|**Video Encoder Configuration**<br>**Device MANDATORY**|**Video Encoder Configuration**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoEncoderConfigurations|Media 2|M|
||GetVideoEncoderConfigurationOptions|Media 2|M|
||SetVideoEncoderConfiguration|Media 2|M|
||tns1:Media/ConfigurationChanged|Event|M|



## 7.12.4 Function list for clients 

|**Video Encoder Configuration**<br>**Client MANDATORY**|**Video Encoder Configuration**<br>**Client MANDATORY**|**Video Encoder Configuration**<br>**Client MANDATORY**|**Video Encoder Configuration**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoEncoderConfigurations|Media 2|O|
||GetVideoEncoderConfigurationOptions|Media 2|M|
||SetVideoEncoderConfiguration|Media 2|M|
||tns1:Media/ConfigurationChanged|Event|O|



www.onvif.org 

34 

ONVIF Profile T Specification v1.0 

## 7.13 Metadata streaming 

This section describes the operations related to metadata streaming. 

## 7.13.1 Device requirements 

- Device shall support listing of **Media Profiles** in response to the **GetProfiles** operation. 

- Device shall return the stream URI in response to the **GetStreamUri** operation. 

- Device shall support initiation of streaming sessions using RTSP according to the **Streaming Service Specification** . 

- Device shall be able to stream metadata over RTP/UDP using the selected **Media Profile** . 

- Device shall be able to stream metadata over RTP/RTSP/HTTP/TCP using the selected **Media Profile** . 

- If supported, device shall be able to stream metadata over RTP/RTSP/HTTPS/TCP using the selected **Media Profile** . 

- If supported, device shall be able to stream metadata over RTP/RTSP/TCP/WebSocket using the selected **Media Profile** . 

- Device shall be able to stream metadata over RTP/UDP multicast using the selected **Media Profile** . 

- Device shall send a key frame on-demand upon reception of the **SetSynchronizationPoint** operation when streaming metadata. The content of the key frame for the metadata stream depends on the filters configured/enabled in MetadataConfiguration such as PTZ Status and Property Events. 

## 7.13.2 Client requirements (if supported) 

- Client shall be able to get the stream URI for the selected profile using the **GetProfiles** and **GetStreamURI** operations. 

- Client shall initiate streaming sessions using RTSP according to the **Streaming Service Specification** . 

- Client shall be able to receive a metadata stream over RTP/UDP or RTP/RTSP/HTTP/TCP using the selected **Media Profile** . 

- If supported, client shall be able to receive a metadata stream over RTP/RTSP/HTTPS/TCP using the selected **Media Profile** . 

- If supported, client shall be able to receive a metadata stream over RTP/UDP multicast using the selected **Media Profile** . 

www.onvif.org 

35 

ONVIF Profile T Specification v1.0 

## 7.13.3 Function list for devices 

|**Metadata Streaming**<br>**Device MANDATORY**|**Metadata Streaming**<br>**Device MANDATORY**|**Metadata Streaming**<br>**Device MANDATORY**|**Metadata Streaming**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetStreamUri|Media 2|M|
||Metadata StreamingusingRTSP|Streaming|M|
||Streamingover RTP/UDP|Streaming|M|
||Streamingover RTP/RTSP/HTTP/TCP|Streaming|M|
||Streamingover RTP/RTSP/HTTPS/TCP|Streaming|C|
||Streamingover RTP/RTSP/TCP/Websocket|Streaming|C|
||Streamingover RTP/UDP Multicast|Streaming|M|
||SetSynchronizationPoint|Media 2|M|



## 7.13.4 Function list for clients 

|**Metadata Streaming**<br>**Client CONDITIONAL**|**Metadata Streaming**<br>**Client CONDITIONAL**|**Metadata Streaming**<br>**Client CONDITIONAL**|**Metadata Streaming**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetStreamUri|Media 2|M|
||Metadata StreamingusingRTSP|Streaming|M|
||Streamingover RTP/UDP|Streaming|M*|
||Streamingover RTP/RTSP/HTTP/TCP|Streaming||
||Streamingover RTP/RTSP/HTTPS/TCP|Streaming|C|
||Streamingover RTP/RTSP/TCP/Websocket|Streaming|O|
||Streamingover RTP/UDP Multicast|Streaming|C|
||SetSynchronizationPoint|Media 2|O|



* Client shall support at least one of the listed transport methods. 

www.onvif.org 

36 

ONVIF Profile T Specification v1.0 

## 7.14 Configuration of metadata profile 

This section describes the operations related to the configuration of **Media Profiles** for metadata streaming. 

## 7.14.1 Device requirements 

- Device shall return available **Media Profiles** in response to the **GetProfiles** operation. 

- Device shall support adding a **Metadata Configuration** to a **Media Profile** using the **GetMetadataConfigurations** and **AddConfiguration** operations. 

- Device shall support removing a **Metadata Configuration** from a profile using the **RemoveConfiguration** operation. 

- Device shall deliver event notifications when a **Metadata Configuration** is added or removed from a **Media Profile** . 

## 7.14.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Media Profiles** using the **GetProfiles** operation. 

- Client shall be able to add a **Metadata Configuration** to a **Media Profile** using the **GetMetadataConfigurations** and **AddConfiguration** operations. 

## 7.14.3 Function list for devices 

|**Configuration of Metadata Profile**<br>**Device MANDATORY**|**Configuration of Metadata Profile**<br>**Device MANDATORY**|**Configuration of Metadata Profile**<br>**Device MANDATORY**|**Configuration of Metadata Profile**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetMetadataConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||RemoveConfiguration|Media 2|M|
||tns1:Media/ProfileChanged|Event|M|



www.onvif.org 

37 

ONVIF Profile T Specification v1.0 

## 7.14.4 Function list for clients 

|**Configuration of Metadata Profile**<br>**Client CONDITIONAL**|**Configuration of Metadata Profile**<br>**Client CONDITIONAL**|**Configuration of Metadata Profile**<br>**Client CONDITIONAL**|**Configuration of Metadata Profile**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetMetadataConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||RemoveConfiguration|Media 2|O|
||tns1:Media/ProfileChanged|Event|O|



www.onvif.org 

38 

ONVIF Profile T Specification v1.0 

## 7.15 Metadata configuration 

This section describes the operations related to metadata configuration. 

## 7.15.1 Device requirements 

- Device shall provide the current **Metadata Configurations** in response to the **GetMetadataConfigurations** operation. 

- Device shall support modifying a **Metadata Configuration** using the **GetMetadataConfigurationOptions** and **SetMetadataConfiguration** operations. 

- Device shall deliver event notifications when a **Metadata Configuration** is changed. 

## 7.15.2 Client requirements (if supported) 

- Client shall be able to retrieve the current **Metadata Configurations** using the **GetMetadataConfigurations** operation. 

- Client shall be able to modify a **Metadata Configuration** using the **SetMetadataConfiguration** operations **.** 

## 7.15.3 Function list for devices 

|**Metadata Configuration**<br>**Device MANDATORY**|**Metadata Configuration**<br>**Device MANDATORY**|**Metadata Configuration**<br>**Device MANDATORY**|**Metadata Configuration**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetMetadataConfigurations|Media 2|M|
||GetMetadataConfigurationOptions|Media 2|M|
||SetMetadataConfiguration|Media 2|M|
||tns1:Media/ConfigurationChanged|Event|M|



## 7.15.4 Function list for clients 

|**Metadata Configuration**<br>**Client CONDITIONAL**|**Metadata Configuration**<br>**Client CONDITIONAL**|**Metadata Configuration**<br>**Client CONDITIONAL**|**Metadata Configuration**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetMetadataConfigurations|Media 2|M|
||GetMetadataConfigurationOptions|Media 2|O|
||SetMetadataConfiguration|Media 2|M|
||tns1:Media/ConfigurationChanged|Event|O|



www.onvif.org 

39 

ONVIF Profile T Specification v1.0 

## 7.16 Imaging settings 

This section describes the operations related to the manipulation of imaging settings. 

## 7.16.1 Device requirements 

- Device shall return available video sources in response to the **GetVideoSources** operation. 

- Device shall support listing of imaging settings using the **GetImagingSettings** operation. 

- Device shall be able to modify imaging settings using the **GetOptions** and **SetImagingSettings** operations. 

## 7.16.2 Client requirements 

- Client shall be able to retrieve current imaging settings using the **GetImagingSettings** operation. 

- Client shall be able to modify imaging settings using the **GetOptions** and **SetImagingSettings** operations. 

## 7.16.3 Function list for devices 

|**Imaging Settings**<br>**Device MANDATORY**|**Imaging Settings**<br>**Device MANDATORY**|**Imaging Settings**<br>**Device MANDATORY**|**Imaging Settings**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoSources|DeviceIO|M|
||GetImagingSettings|Imaging|M|
||GetOptions|Imaging|M|
||SetImagingSettings|Imaging|M|



## 7.16.4 Function list for clients 

|**Imaging Settings**<br>**Client MANDATORY**|**Imaging Settings**<br>**Client MANDATORY**|**Imaging Settings**<br>**Client MANDATORY**|**Imaging Settings**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoSources|DeviceIO|O|
||GetImagingSettings|Imaging|M|
||GetOptions|Imaging|M|
||SetImagingSettings|Imaging|M|



www.onvif.org 

40 

ONVIF Profile T Specification v1.0 

## 7.17 Tampering 

This section describes the operations related to tampering. 

## 7.17.1 Device requirements 

- Device shall generate at least one type of Tampering event according to the **Imaging Service Specification** . 

## 7.17.2 Client requirements (if supported) 

- Client shall be able to receive all types of Tampering events according to the **Imaging Service Specification** . 

## 7.17.3 Function list for devices 

|**Tampering**<br>**Device MANDATORY**|**Tampering**<br>**Device MANDATORY**|**Tampering**<br>**Device MANDATORY**|**Tampering**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||tns1:VideoSource/ImageTooBlurry/ImagingService|Event|M*|
||tns1:VideoSource/ImageTooBlurry/AnalyticsService|Event||
||tns1:VideoSource/ImageTooDark/ImagingService|Event||
||tns1:VideoSource/ImageTooDark/AnalyticsService|Event||
||tns1:VideoSource/ImageTooBright/ImagingService|Event||
||tns1:VideoSource/ImageTooBright/AnalyticsService|Event||
||tns1:VideoSource/GlobalSceneChange/ImagingService|Event||
||tns1:VideoSource/GlobalSceneChange/AnalyticsService|Event||



* Device shall support at least one of the listed event topics. 

## 7.17.4 Function list for clients 

|**Tampering**<br>**Client CONDITIONAL**|**Tampering**<br>**Client CONDITIONAL**|**Tampering**<br>**Client CONDITIONAL**|**Tampering**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||tns1:VideoSource/ImageTooBlurry/ImagingService|Event|M|
||tns1:VideoSource/ImageTooBlurry/AnalyticsService|Event|M|
||tns1:VideoSource/ImageTooDark/ImagingService|Event|M|
||tns1:VideoSource/ImageTooDark/AnalyticsService|Event|M|
||tns1:VideoSource/ImageTooBright/ImagingService|Event|M|
||tns1:VideoSource/ImageTooBright/AnalyticsService|Event|M|
||tns1:VideoSource/GlobalSceneChange/ImagingService|Event|M|
||tns1:VideoSource/GlobalSceneChange/AnalyticsService|Event|M|



www.onvif.org 

41 

ONVIF Profile T Specification v1.0 

## 7.18 Configuration of On-Screen Display (OSD) 

This section describes the operations related to the configuration of the On-Screen Display (OSD). It also covers adding and removing OSDs in **Media Profiles** . 

## 7.18.1 Device requirements 

- Device shall support listing of **Video Source Configurations** using the **GetVideoSourceConfigurations** operation. 

- Device shall be able to create OSD text configurations using the **CreateOSD** operation. 

- If supported, device shall be able to create OSD image configurations using the **CreateOSD** operation. 

- Device shall support deletion of OSDs using the **DeleteOSD** operation. 

- Device shall support listing of OSDs using the **GetOSDs** operation. 

- Device shall support modification of an OSD using the **GetOSDOptions** and **SetOSD** operations. 

## 7.18.2 Client requirements (if supported) 

- Client shall be able to create OSD text configurations using the **CreateOSD** operation. 

- If supported, client shall be able to create OSD image configurations using the **CreateOSD** operation. 

- Client shall be able to retrieve OSDs using the **GetVideoSourceConfigurations** , **GetOSDs** operation. 

- Client shall be able to modify an OSD using the **GetOSDOptions** and **SetOSD** operations. 

## 7.18.3 Function list for devices 

|**Configuration of On-Screen Display**<br>**Device MANDATORY**|**Configuration of On-Screen Display**<br>**Device MANDATORY**|**Configuration of On-Screen Display**<br>**Device MANDATORY**|**Configuration of On-Screen Display**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||CreateOSD, text|Media 2|M|
||CreateOSD, image|Media 2|C|
||DeleteOSD|Media 2|M|
||GetVideoSourceConfigurations|Media 2|M|
||GetOSDs|Media 2|M|
||GetOSDOptions|Media 2|M|
||SetOSD|Media 2|M|



www.onvif.org 

42 

ONVIF Profile T Specification v1.0 

## 7.18.4 Function list for clients 

|**Configuration of On-Screen Display**<br>**Client CONDITIONAL**|**Configuration of On-Screen Display**<br>**Client CONDITIONAL**|**Configuration of On-Screen Display**<br>**Client CONDITIONAL**|**Configuration of On-Screen Display**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||CreateOSD, text|Media 2|M|
||CreateOSD, image|Media 2|C|
||DeleteOSD|Media 2|O|
||GetVideoSourceConfigurations|Media 2|M|
||GetOSDs|Media 2|M|
||GetOSDOptions|Media 2|M|
||SetOSD|Media 2|M|



www.onvif.org 

43 

ONVIF Profile T Specification v1.0 

## 7.19 JPEG snapshot 

This section describes the operations related to the providing of a JPEG image snapshot by a device. 

## 7.19.1 Device requirements 

- Device shall provide a JPEG snapshot URI in response to the **GetSnapshotUri** operation. 

## 7.19.2 Function list for devices 

|**JPEG Snapshot**<br>**Device MANDATORY**|**JPEG Snapshot**<br>**Device MANDATORY**|**JPEG Snapshot**<br>**Device MANDATORY**|**JPEG Snapshot**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetSnapshotUri|Media 2|M|



## 7.19.3 Function list for clients 

|**JPEG Snapshot**<br>**Client OPTIONAL**|**JPEG Snapshot**<br>**Client OPTIONAL**|**JPEG Snapshot**<br>**Client OPTIONAL**|**JPEG Snapshot**<br>**Client OPTIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetSnapshotUri|Media 2|O|



www.onvif.org 

44 

ONVIF Profile T Specification v1.0 

## 7.20 Motion alarm events 

This section describes the operations related to the **Motion Alarm** event. 

## 7.20.1 Device requirements 

- Device shall generate **Motion Alarm** events according to the **Imaging Service Specification** . 

## 7.20.2 Client requirements 

- Clients shall receive notifications of **Motion Alarm** events according to the **Imaging Service Specification** . 

## 7.20.3 Function list for devices 

|**Motion Alarm Events**<br>**Device MANDATORY**|**Motion Alarm Events**<br>**Device MANDATORY**|**Motion Alarm Events**<br>**Device MANDATORY**|**Motion Alarm Events**<br>**Device MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||tns1:VideoSource/MotionAlarm|Event|M|



## 7.20.4 Function list for clients 

|**Motion Alarm Events**<br>**Client MANDATORY**|**Motion Alarm Events**<br>**Client MANDATORY**|**Motion Alarm Events**<br>**Client MANDATORY**|**Motion Alarm Events**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||tns1:VideoSource/MotionAlarm|Event|M|



www.onvif.org 

45 

ONVIF Profile T Specification v1.0 

## 7.21 Absolute PTZ move 

This section describes the requirements for moving a PTZ device to an absolute position. This section covers devices with motors (mechanical PTZ), devices without motors (digital PTZ), and clients that communicate with each category of device. 

Some devices only support Pan/Tilt and not Zoom (or vice versa). For this reason, Pan/Tilt operations are listed separately from Zoom operations. To accommodate non-zoom devices, device zoom operations are listed as Conditional. 

## 7.21.1 Device requirements (if supported) 

- Device shall provide at least one ready-to-use **Media Profile** for PTZ control per PTZ node. 

- Device shall return true for the capability **MoveStatus** and **StatusPosition** in the response to the **GetServiceCapabilities** operation. 

- Device shall support providing PTZ status through the **GetStatus** operation. 

- Device shall support the **AbsoluteMove** operation. 

- A device that supports motorized pan/tilt shall have a PTZ node that lists the following pan/tilt PTZ spaces in the **SupportedPTZSpaces** capability: 

   - http://www.onvif.org/ver10/tptz/PanTiltSpaces/SphericalPositionSpaceDegrees 

- A device that supports pan/tilt shall have a PTZ node that lists the following pan/tilt PTZ spaces in the **SupportedPTZSpaces** capability: 

   - http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace 

- A device that supports zoom shall have a PTZ node that lists the following zoom PTZ spaces in the **SupportedPTZSpaces** capability: 

   - http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace 

## 7.21.2 Client requirements 

- Client shall be able to move a PTZ device using the **AbsoluteMove** operation using the following PTZ spaces: 

   - http://www.onvif.org/ver10/tptz/PanTiltSpaces/SphericalPositionSpaceDegrees 

   - http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace 

   - http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace 

www.onvif.org 

46 

ONVIF Profile T Specification v1.0 

## 7.21.3 Function list for devices 

|**Absolute PTZ Move**<br>**Device CONDITIONAL**|**Absolute PTZ Move**<br>**Device CONDITIONAL**|**Absolute PTZ Move**<br>**Device CONDITIONAL**|**Absolute PTZ Move**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||AbsoluteMove|PTZ|M|
||GetStatus|PTZ|M|
||http://www.onvif.org/ver10/tptz/PanTiltSpaces/SphericalPositionSpaceDegrees|PTZ|C|
||http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace|PTZ|C|
||http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace|PTZ|C|



## 7.21.4 Function list for clients 

|**Absolute PTZ Move**<br>**Client MANDATORY**|**Absolute PTZ Move**<br>**Client MANDATORY**|**Absolute PTZ Move**<br>**Client MANDATORY**|**Absolute PTZ Move**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||AbsoluteMove|PTZ|M|
||GetStatus|PTZ|O|
||http://www.onvif.org/ver10/tptz/PanTiltSpaces/SphericalPositionSpaceDegrees|PTZ|M|
||http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace|PTZ|M|
||http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace|PTZ|M|



www.onvif.org 

47 

ONVIF Profile T Specification v1.0 

## 7.22 Continuous PTZ move 

This section details the requirements for performing a continuous move operation on a PTZ device, and stopping that move operation. Unlike the Absolute Move section, this section does not distinguish between devices with and without motors since the namespaces and functions are the same for both categories. 

Some devices only support Pan/Tilt and not Zoom (or vice versa). For this reason, Pan/Tilt operations are listed separately from Zoom operations. To accommodate non-zoom devices, device zoom operations are listed as Conditional. 

## 7.22.1 Device requirements (if supported) 

- Device shall provide at least one ready-to-use **Media Profile** for PTZ control per PTZ node. 

- Device shall return true for the capability **MoveStatus** in the response to the **GetServiceCapabilities** operation. 

- Device shall support providing PTZ status through the **GetStatus** operation. 

- Device shall support the **ContinuousMove** and **Stop** operations. 

- A device that supports pan/tilt shall have a PTZ node that lists the following pan/tilt PTZ space in the **SupportedPTZSpaces** capability: 

   - http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace 

- A device that supports zoom shall have a PTZ node that lists the following zoom PTZ space in the **SupportedPTZSpaces** capability: 

   - http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace 

## 7.22.2 Client requirements 

- Client shall be able to move a PTZ device using the **ContinuousMove** operation using the following PTZ spaces 

   - http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace 

   - http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace 

- Client shall be able to stop a continuous move using the **Stop** operation. 

www.onvif.org 

48 

ONVIF Profile T Specification v1.0 

## 7.22.3 Function list for devices 

|**Continuous PTZ Move**<br>**Device CONDITIONAL**|**Continuous PTZ Move**<br>**Device CONDITIONAL**|**Continuous PTZ Move**<br>**Device CONDITIONAL**|**Continuous PTZ Move**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||ContinuousMove|PTZ|M|
||Stop|PTZ|M|
||GetStatus|PTZ|M|
||http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace|PTZ|C|
||http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace|PTZ|C|



## 7.22.4 Function list for clients 

|**Continuous PTZ Move**<br>**Client MANDATORY**|**Continuous PTZ Move**<br>**Client MANDATORY**|**Continuous PTZ Move**<br>**Client MANDATORY**|**Continuous PTZ Move**<br>**Client MANDATORY**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||ContinuousMove|PTZ|M|
||Stop|PTZ|M|
||GetStatus|PTZ|O|
||http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace|PTZ|M|
||http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace|PTZ|M|



www.onvif.org 

49 

ONVIF Profile T Specification v1.0 

## **8 Profile conditional features (normative)** 

The Profile Conditional Features section lists the features that shall be implemented if the device or client supports the feature. The requirements represent the minimum functionality that must be implemented for conformance. 

www.onvif.org 

50 

ONVIF Profile T Specification v1.0 

## 8.1 Configuration of PTZ profile 

This section describes the operations related to the configuration of **Media Profiles** for PTZ operations. 

The reader should be familiar with the PTZ spaces defined in the **PTZ Service Specification** , and the functions defined in each PTZ namespace. For example, when using the PositionGenericSpace, some calculation may be required using the range of values for each axis, as returned by the GetConfigurationOptions command response from the PTZ service. 

## 8.1.1 Device requirements (if supported) 

- Device shall return the set of available **Media Profiles** in response to the **GetProfiles** operation. 

- Device shall support adding a **PTZ Configuration** to a **Media Profile** using the **GetCompatibleConfigurations** (from the PTZ Service) and **AddConfiguration** operations. 

- Device shall support removing a **PTZ Configuration** from a profile using the **RemoveConfiguration** operation. 

- Device shall deliver event notifications when a **PTZ Configuration** is added or removed from a **Media Profile** . 

## 8.1.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Media Profiles** using the **GetProfiles** operation. 

- Client shall be able to add a **PTZ Configuration** to a **Media Profile** using the **GetCompatibleConfigurations** (from the PTZ Service) and **AddConfiguration** operations. 

## 8.1.3 Function list for devices 

|**Configuration of PTZ Profile**<br>**Device CONDITIONAL**|**Configuration of PTZ Profile**<br>**Device CONDITIONAL**|**Configuration of PTZ Profile**<br>**Device CONDITIONAL**|**Configuration of PTZ Profile**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetCompatibleConfigurations|PTZ|M|
||AddConfiguration|Media 2|M|
||RemoveConfiguration|Media 2|M|
||tns1:Media/ProfileChanged|Event|M|



www.onvif.org 

51 

ONVIF Profile T Specification v1.0 

## 8.1.4 Function list for clients 

|**Configuration of PTZ Profile**<br>**Client CONDITIONAL**|**Configuration of PTZ Profile**<br>**Client CONDITIONAL**|**Configuration of PTZ Profile**<br>**Client CONDITIONAL**|**Configuration of PTZ Profile**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetCompatibleConfigurations|PTZ|M|
||AddConfiguration|Media 2|M|
||RemoveConfiguration|Media 2|O|
||tns1:Media/ProfileChanged|Event|O|



www.onvif.org 

52 

ONVIF Profile T Specification v1.0 

## 8.2 PTZ configuration 

This section describes the operations related to PTZ configuration. 

## 8.2.1 Device requirements (if supported) 

- Device shall return its PTZ nodes in response to the **GetNode** and **GetNodes** operations. 

- Device shall return available PTZ configuration options in response to the **GetConfigurationOptions** operation. 

- Device shall support modifying a PTZ configuration in response to the **SetConfiguration** operation. 

- Device shall deliver event notifications when a PTZ Configuration is changed. 

## 8.2.2 Client requirements (if supported) 

- Client shall be able to retrieve PTZ nodes using at least one of the operations **GetNode** and **GetNodes** . 

- Client shall be able to modify a PTZ configuration using the **SetConfiguration** operation. 

## 8.2.3 Function list for devices 

|**PTZ Configuration**<br>**Device CONDITIONAL**|**PTZ Configuration**<br>**Device CONDITIONAL**|**PTZ Configuration**<br>**Device CONDITIONAL**|**PTZ Configuration**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetNodes|PTZ|M|
||GetNode|PTZ|M|
||GetConfigurationOptions|PTZ|M|
||SetConfiguration|PTZ|M|
||tns1:Media/ConfigurationChanged|Event|M|



## 8.2.4 Function list for clients 

|**PTZ Configuration**<br>**Client CONDITIONAL**|**PTZ Configuration**<br>**Client CONDITIONAL**|**PTZ Configuration**<br>**Client CONDITIONAL**|**PTZ Configuration**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetNodes|PTZ|M*|
||GetNode|PTZ||
||GetConfigurationOptions|PTZ|O|
||SetConfiguration|PTZ|M|
||tns1:Media/ConfigurationChanged|Event|O|



* Client shall support at least one of the listed operations. 

www.onvif.org 

53 

ONVIF Profile T Specification v1.0 

## 8.3 PTZ presets 

This section describes the operations related to moving a device to a PTZ preset, and listing, configuring and removing PTZ presets. 

## 8.3.1 Device requirements (if supported) 

- Device shall provide a PTZ node with the **MaximumNumberOfPresets** capability set to at least 1. 

- Device shall return available presets in response to the **GetPresets** operation. 

- Device shall move to a specific preset in response to the **GotoPreset** operation. 

- Device shall support storing the current position to a preset in response to the **SetPreset** operation. 

- Device shall support removing a stored preset in response to the **RemovePreset** operation. 

## 8.3.2 Client requirements (if supported) 

- Client shall be able to retrieve available presets using the **GetPresets** operation. 

- Client shall be able to move a PTZ device to a specific preset using the **GotoPreset** operation. 

- Client shall be able to store a preset using the **SetPreset** operation **.** 

www.onvif.org 

54 

ONVIF Profile T Specification v1.0 

## 8.3.3 Function list for devices 

|**PTZ Presets**<br>**Device CONDITIONAL**|**PTZ Presets**<br>**Device CONDITIONAL**|**PTZ Presets**<br>**Device CONDITIONAL**|**PTZ Presets**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetPresets|PTZ|M|
||SetPreset|PTZ|M|
||GotoPreset|PTZ|M|
||RemovePreset|PTZ|M|



## 8.3.4 Function list for clients 

|**PTZ Presets**<br>**Client CONDITIONAL**|**PTZ Presets**<br>**Client CONDITIONAL**|**PTZ Presets**<br>**Client CONDITIONAL**|**PTZ Presets**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetPresets|PTZ|M|
||SetPreset|PTZ|M|
||GotoPreset|PTZ|M|
||RemovePreset|PTZ|O|



www.onvif.org 

55 

ONVIF Profile T Specification v1.0 

## 8.4 PTZ home position 

This section describes the operations related to PTZ home position. 

## 8.4.1 Device requirements (if supported) 

- Device shall provide a PTZ node with the **HomeSupported** capability set to true. 

- Device shall set its home position in response to the **SetHomePosition** operation. 

- Device shall support moving to its home position in response to the **GotoHomePosition** operation. 

## 8.4.2 Client requirements (if supported) 

- Client shall be able to move a PTZ device to its home position using the **GotoHomePosition** operation. 

## 8.4.3 Function list for devices 

|**PTZ Home Position**<br>**Device CONDITIONAL**|**PTZ Home Position**<br>**Device CONDITIONAL**|**PTZ Home Position**<br>**Device CONDITIONAL**|**PTZ Home Position**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||SetHomePosition|PTZ|M|
||GotoHomePosition|PTZ|M|



## 8.4.4 Function list for clients 

|**PTZ Home Position**<br>**Client CONDITIONAL**|**PTZ Home Position**<br>**Client CONDITIONAL**|**PTZ Home Position**<br>**Client CONDITIONAL**|**PTZ Home Position**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||SetHomePosition|PTZ|O|
||GotoHomePosition|PTZ|M|



www.onvif.org 

56 

ONVIF Profile T Specification v1.0 

## 8.5 Configuration of analytics profile 

This section describes the operations related to the configuration of **Media Profiles** for streaming analytics metadata. 

## 8.5.1 Device requirements (if supported) 

- Device shall return the set of available **Media Profiles** in response to the **GetProfiles** operation. 

- Device shall support adding an **Analytics Configuration** to a **Media Profile** using the **GetAnalyticsConfigurations** and **AddConfiguration** operations. 

- Device shall support removing an **Analytics Configuration** from a profile using the **RemoveConfiguration** operation. 

- Device shall deliver event notifications when an **Analytics Configuration** is added or removed from a **Media Profile** . 

## 8.5.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Media Profiles** using the **GetProfiles** operation. 

- Client shall be able to add an **Analytics Configuration** to a **Media Profile** using the **GetAnalyticsConfigurations** and **AddConfiguration** operations. 

## 8.5.3 Function list for devices 

|**Configuration of Analytics Profile**<br>**Device CONDITIONAL**|**Configuration of Analytics Profile**<br>**Device CONDITIONAL**|**Configuration of Analytics Profile**<br>**Device CONDITIONAL**|**Configuration of Analytics Profile**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetAnalyticsConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||RemoveConfiguration|Media 2|M|
||tns1:Media/ProfileChanged|Event|M|



www.onvif.org 

57 

ONVIF Profile T Specification v1.0 

## 8.5.4 Function list for clients 

|**Configuration of Analytics Profile**<br>**Client CONDITIONAL**|**Configuration of Analytics Profile**<br>**Client CONDITIONAL**|**Configuration of Analytics Profile**<br>**Client CONDITIONAL**|**Configuration of Analytics Profile**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetAnalyticsConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||RemoveConfiguration|Media 2|O|
||tns1:Media/ProfileChanged|Event|O|



www.onvif.org 

58 

ONVIF Profile T Specification v1.0 

## 8.6 Motion region detector configuration 

This section describes the operations related to Motion Region Detector rule configuration and event notification. 

## 8.6.1 Device requirements (if supported) 

- Device shall include **tt:MotionRegionDetector** in response to the **GetSupportedRules** operation. 

- Device shall return available **Rules** in response to the **GetRules** operation. 

- Device shall support creation of **Rules** in response to the **GetRuleOptions** and **CreateRules** operation. 

- Device shall support modification of **Rules** in response to the **GetRuleOptions** and **ModifyRules** operation. 

- Device shall support deletion of **Rules** in response to the **DeleteRules** operation. 

- Device shall generate **Motion Region Detector** events according to the **Analytics Service Specification** . 

## 8.6.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Rules** using the **GetSupportedRules** and **GetRules** operations. 

- Client shall be able to create **Rules** of type **tt:MotionRegionDetector** using the **GetRuleOptions** and **CreateRules** operation. 

- Client shall be able to delete **Rules** using the **DeleteRules** operation. 

- Clients shall receive notifications of **Motion Region Detector** events according to the **Analytics Service Specification** . 

www.onvif.org 

59 

ONVIF Profile T Specification v1.0 

## 8.6.3 Function list for devices 

|**Motion Region Detector Configuration**<br>**Device CONDITIONAL**|**Motion Region Detector Configuration**<br>**Device CONDITIONAL**|**Motion Region Detector Configuration**<br>**Device CONDITIONAL**|**Motion Region Detector Configuration**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetSupportedRules|Analytics|M|
||GetRules|Analytics|M|
||GetRuleOptions|Analytics|M|
||CreateRules|Analytics|M|
||ModifyRules|Analytics|M|
||DeleteRules|Analytics|M|
||tns1:RuleEngine/MotionRegionDetector/Motion|Event|M|



## 8.6.4 Function list for clients 

|**Motion Region Detector Configuration**<br>**Client CONDITIONAL**|**Motion Region Detector Configuration**<br>**Client CONDITIONAL**|**Motion Region Detector Configuration**<br>**Client CONDITIONAL**|**Motion Region Detector Configuration**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetSuportedRules|Analytics|M|
||GetRules|Analytics|M|
||GetRuleOptions|Analytics|M|
||CreateRules|Analytics|M|
||ModifyRules|Analytics|O|
||DeleteRules|Analytics|M|
||tns1:RuleEngine/MotionRegionDetector/Motion|Event|M|



www.onvif.org 

60 

ONVIF Profile T Specification v1.0 

## 8.7 Video source mode 

This section describes the operations related to video source mode. 

## 8.7.1 Device requirements (if supported) 

- Device shall return available video sources in response to the **GetVideoSources** operation. 

- Device shall return the information for current video source mode and settable video source modes of specified video source in response to the **GetVideoSourceModes** operation. 

- Device shall change its current video source mode in response to the **SetVideoSourceMode** operation. 

## 8.7.2 Client requirements (if supported) 

- Client shall request the information for current video source mode and settable video source modes of specified video source using the **GetVideoSourceModes** operation. 

- Client shall be able to change current video source mode using the **SetVideoSourceMode** operation. 

## 8.7.3 Function list for devices 

|**Video Source Mode**<br>**Device CONDITIONAL**|**Video Source Mode**<br>**Device CONDITIONAL**|**Video Source Mode**<br>**Device CONDITIONAL**|**Video Source Mode**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoSources|DeviceIO|M|
||GetVideoSourceModes|Media 2|M|
||SetVideoSourceMode|Media 2|M|



## 8.7.4 Function list for clients 

|**Video Source Mode**<br>**Client CONDITIONAL**|**Video Source Mode**<br>**Client CONDITIONAL**|**Video Source Mode**<br>**Client CONDITIONAL**|**Video Source Mode**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoSources|DeviceIO|O|
||GetVideoSourceModes|Media 2|M|
||SetVideoSourceMode|Media 2|M|



www.onvif.org 

61 

ONVIF Profile T Specification v1.0 

## 8.8 NTP 

This section describes the operations related to synchronization of time on a Device using NTP servers. 

## 8.8.1 Device requirements (if supported) 

- Device shall support configuring **NTP** servers in response to the **GetNTP** and **SetNTP** operations. 

## 8.8.2 Client requirements (if supported) 

- Client shall be able to configure **NTP** servers on a device using the **GetNTP** and **SetNTP** operations. 

## 8.8.3 Function list for devices 

|**NTP**<br>**Device CONDITIONAL**|**NTP**<br>**Device CONDITIONAL**|**NTP**<br>**Device CONDITIONAL**|**NTP**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetNTP|Device Management|M|
||SetNTP|Device Management|M|



## 8.8.4 Function list for clients 

|**NTP**<br>**Client CONDITIONAL**|**NTP**<br>**Client CONDITIONAL**|**NTP**<br>**Client CONDITIONAL**|**NTP**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetNTP|Device Management|M|
||SetNTP|Device Management|M|



www.onvif.org 

62 

ONVIF Profile T Specification v1.0 

## 8.9 Audio streaming 

This section describes the operations related to the setup and control of audio streaming. 

## 8.9.1 Device requirements (if supported) 

- Device shall support providing the stream URI for the selected **Media Profile** using the **GetProfiles** and **GetStreamURI** operations. 

- Device shall support streaming of at least one of the G.711 µ-law and AAC encoding formats. 

- Device shall support initiation of streaming sessions using RTSP according to the **Streaming Service Specification** . 

- Device shall be able to stream audio over RTP/UDP using the selected **Media Profile** . 

- Device shall be able to stream audio over RTP/RTSP/HTTP/TCP using the selected **Media Profile** . 

- If supported, device shall be able to stream audio over RTP/RTSP/HTTPS/TCP using the selected **Media Profile** . 

- If supported, device shall be able to stream audio over RTP/RTSP/TCP/WebSocket **,** using the selected **Media Profile.** 

- Device shall be able to stream audio over RTP/UDP multicast using the selected **Media Profile** . 

## 8.9.2 Client requirements (if supported) 

- Client shall be able to get the stream URI for the selected **Media Profile** using the **GetProfiles** and **GetStreamURI** operations. 

- Client shall initiate streaming sessions using RTSP according to the **Streaming Service Specification** . 

- Client shall be able to receive a stream and decode G.711 µ-law audio using the selected **Media Profile** . 

- Client shall be able to receive a stream and decode AAC audio using the selected **Media Profile** . 

- Client shall be able to receive an audio stream over RTP/UDP or RTP/RTSP/HTTP/TCP using the selected **Media Profile** . 

- If supported, client shall be able to receive an audio stream over RTP/RTSP/HTTPS/TCP using the selected **Media Profile** . 

www.onvif.org 

63 

ONVIF Profile T Specification v1.0 

- If supported, client shall be able to receive an audio stream over RTP/UDP multicast using the selected **Media Profile** . 

## 8.9.3 Function list for devices 

|**Audio Streaming**<br>**Device CONDITIONAL**|**Audio Streaming**<br>**Device CONDITIONAL**|**Audio Streaming**<br>**Device CONDITIONAL**|**Audio Streaming**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetStreamUri|Media 2|M|
||Audio StreamingusingRTSP|Streaming|M|
||G.711µ-law Encoding|Media 2|M*|
||AAC Encoding|Media 2||
||Streamingover RTP/UDP|Streaming|M|
||Streamingover RTP/RTSP/HTTP/TCP|Streaming|M|
||Streamingover RTP/RTSP/HTTPS/TCP|Streaming|C|
||Streamingover RTP/RTSP/TCP/Websocket|Streaming|C|
||Streamingover RTP/UDP Multicast|Streaming|M|



- Device shall support at least one of the listed encoding formats. 

## 8.9.4 Function list for clients 

|**Audio Streaming**<br>**Client CONDITIONAL**|**Audio Streaming**<br>**Client CONDITIONAL**|**Audio Streaming**<br>**Client CONDITIONAL**|**Audio Streaming**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetStreamUri|Media 2|M|
||Audio StreamingusingRTSP|Streaming|M|
||G.711µ-law Decoding|Media 2|M|
||AAC Decoding|Media 2|M|
||Streamingover RTP/UDP|Streaming|M*|
||Streamingover RTP/RTSP/HTTP/TCP|Streaming||
||Streamingover RTP/RTSP/HTTPS/TCP|Streaming|C|
||Streamingover RTP/RTSP/TCP/Websocket|Streaming|O|
||Streamingover RTP/UDP Multicast|Streaming|C|



- Client shall support at least one of the listed transport methods. 

www.onvif.org 

64 

ONVIF Profile T Specification v1.0 

## 8.10 Configuration of audio profile 

This section describes the operations related to configuring **Media Profiles** for audio streaming. 

## 8.10.1 Device requirements (if supported) 

- Device shall support listing of **Media Profiles** in response to the **GetProfiles** operation. 

- Device shall support listing of audio sources in response to the **GetAudioSources** operation. 

- Device shall support adding an **Audio Source Configuration** to a **Media Profile** using the **GetAudioSourceConfigurations** and **AddConfiguration** operations. 

- Device shall support adding an **Audio Encoder Configuration** to a **Media Profile** using the **GetAudioEncoderConfigurations** and **AddConfiguration** operations. 

- Device shall support removing an **Audio Source Configuration** or an **Audio Encoder Configuration** from a profile using the **RemoveConfiguration** operation. 

- Device shall deliver event notifications when an **Audio Source Configuration** or **Audio Encoder Configuration** is added or removed from a **Media Profile** . 

## 8.10.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Media Profiles** using the **GetProfiles** operation. 

- Client shall be able to either: 

   - Add an **Audio Source Configuration** to a **Media Profile** using the **GetAudioSourceConfigurations** and **AddConfiguration** operations, or 

   - Create a media profile with an **Audio Source Configuration** according to **7.8.2** . 

- Client shall be able to add an **Audio Encoder Configuration** to a **Media Profile** using the **GetAudioEncoderConfigurations** and **AddConfiguration** operations. 

www.onvif.org 

65 

ONVIF Profile T Specification v1.0 

## 8.10.3 Function list for devices 

|**Configuration of Audio Profile**<br>**Device CONDITIONAL**|**Configuration of Audio Profile**<br>**Device CONDITIONAL**|**Configuration of Audio Profile**<br>**Device CONDITIONAL**|**Configuration of Audio Profile**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetAudioSources|DeviceIO|M|
||GetAudioSourceConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||GetAudioEncoderConfigurations|Media 2|M|
||RemoveConfiguration|Media 2|M|
||tns1:Media/ProfileChanged|Event|M|



## 8.10.4 Function list for clients 

|**Configuration of Audio Profile**<br>**Client CONDITIONAL**|**Configuration of Audio Profile**<br>**Client CONDITIONAL**|**Configuration of Audio Profile**<br>**Client CONDITIONAL**|**Configuration of Audio Profile**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetAudioSources|DeviceIO|O|
||GetAudioSourceConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||GetAudioEncoderConfigurations|Media 2|M|
||RemoveConfiguration|Media 2|O|
||tns1:Media/ProfileChanged|Event|O|



www.onvif.org 

66 

ONVIF Profile T Specification v1.0 

## 8.11 Audio encoder configuration 

This section describes the operations related to modifying audio encoder configurations. 

## 8.11.1 Device requirements (if supported) 

- Device shall support listing of **Audio Encoder Configurations** in response to the **GetAudioEncoderConfigurations** operation. 

- Device shall support modifying an **Audio Encoder Configuration** using the **GetAudioEncoderConfigurationOptions** and **SetAudioEncoderConfiguration** operations. 

- Device shall deliver event notifications when an **Audio Encoder Configuration** is changed. 

## 8.11.2 Client requirements (if supported) 

- Client shall be able to retrieve the current **Audio Encoder Configurations** using the **GetAudioEncoderConfigurations** operation. 

- Client shall be able to modify an **Audio Encoder Configuration** using the **GetAudioEncoderConfigurationOptions** and **SetAudioEncoderConfiguration** operations **.** 

## 8.11.3 Function list for devices 

|**Audio Encoder Configuration**<br>**Device CONDITIONAL**|**Audio Encoder Configuration**<br>**Device CONDITIONAL**|**Audio Encoder Configuration**<br>**Device CONDITIONAL**|**Audio Encoder Configuration**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetAudioEncoderConfigurations|Media 2|M|
||GetAudioEncoderConfigurationOptions|Media 2|M|
||SetAudioEncoderConfiguration|Media 2|M|
||tns1:Media/ConfigurationChanged|Event|M|



## 8.11.4 Function list for clients 

|**Audio Encoder Configuration**<br>**Client CONDITIONAL**|**Audio Encoder Configuration**<br>**Client CONDITIONAL**|**Audio Encoder Configuration**<br>**Client CONDITIONAL**|**Audio Encoder Configuration**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetAudioEncoderConfigurations|Media 2|M|
||GetAudioEncoderConfigurationOptions|Media 2|M|
||SetAudioEncoderConfiguration|Media 2|M|
||tns1:Media/ConfigurationChanged|Event|O|



www.onvif.org 

67 

ONVIF Profile T Specification v1.0 

## 8.12 Audio output streaming 

This section describes the operations related to audio output streaming. It is also known as audio backchannel. 

## 8.12.1 Device requirements (if supported) 

- Device shall support getting the stream URI for the selected **Media Profile** using the **GetProfiles** and **GetStreamURI** operations. 

- Device shall return the list of decoder options in response to the **GetAudioDecoderConfigurationOptions** operation. 

- Device shall support initiation of streaming sessions using RTSP according to the **Streaming Service Specification** , Back Channel Connection. 

- Device shall be able to decode G.711 µ-law. 

- If supported, device shall be able to decode AAC. 

- Device shall be able to receive an audio stream over RTP/UDP and RTP/RTSP/HTTP/TCP using the selected **Media Profile** . 

- If supported, device shall be able to receive an audio stream over RTP/RTSP/HTTPS/TCP using the selected **Media Profile** . 

- If supported, device shall be able receive an audio stream over RTP/RTSP/TCP/WebSocket using the selected **Media Profile** . 

## 8.12.2 Client requirements (if supported) 

- Client shall be able to get the stream URI for the selected **Media Profile** using the **GetProfiles** and **GetStreamURI** operations. 

- Client shall be able to initiate streaming sessions using RTSP according to the **Streaming Service Specification** , Back Channel Connection. 

- Client shall be able to send a stream of G.711 µ-law encoded audio **.** 

- If supported, client shall be able to send a stream of AAC encoded audio **.** 

- Client shall be able to stream audio over RTP/UDP or RTP/RTSP/HTTP/TCP using the selected **Media Profile** . 

- If supported, client shall be able to stream audio over RTP/RTSP/HTTPS/TCP using the selected **Media Profile** . 

www.onvif.org 

68 

ONVIF Profile T Specification v1.0 

## 8.12.3 Function list for devices 

|**Audio Output Streaming**<br>**Device CONDITIONAL**|**Audio Output Streaming**<br>**Device CONDITIONAL**|**Audio Output Streaming**<br>**Device CONDITIONAL**|**Audio Output Streaming**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetStreamUri|Media 2|M|
||GetAudioDecoderConfigurationOptions|Media 2|M|
||StreamingusingRTSP – Back Channel|Streaming|M|
||G.711µ-law Decoding|Media 2|M|
||AAC Decoding|Media 2|C|
||Streamingover RTP/UDP|Streaming|M|
||Streamingover RTP/RTSP/HTTP/TCP|Streaming|M|
||Streamingover RTP/RTSP/HTTPS/TCP|Streaming|C|
||Streamingover RTP/RTSP/TCP/WebSocket|Streaming|C|



## 8.12.4 Function list for clients 

|**Audio Output Streaming**<br>**Client CONDITIONAL**|**Audio Output Streaming**<br>**Client CONDITIONAL**|**Audio Output Streaming**<br>**Client CONDITIONAL**|**Audio Output Streaming**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetStreamUri|Media 2|M|
||GetAudioDecoderConfigurationOptions|Media 2|O|
||StreamingusingRTSP – Back Channel|Streaming|M|
||G.711µ-law Encoding|Media 2|M|
||AAC Encoding|Media 2|C|
||Streamingover RTP/UDP|Streaming|M*|
||Streamingover RTP/RTSP/HTTP/TCP|Streaming||
||Streamingover RTP/RTSP/HTTPS/TCP|Streaming|C|
||Streamingover RTP/RTSP/TCP/WebSocket|Streaming|O|



- Client shall support at least one of the listed transport methods. 

www.onvif.org 

69 

ONVIF Profile T Specification v1.0 

## 8.13 Configuration of audio output profile 

This section describes the operations related to the configuration of **Media Profiles** for audio output streaming (audio backchannel). 

## 8.13.1 Device requirements (if supported) 

- Device shall support listing of **Media Profiles** in response to the **GetProfiles** operation. 

- Device shall support listing of audio outputs in response to the **GetAudioOutputs** operation. 

- Device shall support adding an **Audio Output Configuration** to a **Media Profile** using the **GetAudioOutputConfigurations** and **AddConfiguration** operations. 

- Device shall support adding an **Audio Decoder Configuration** to a **Media Profile** using the **GetAudioDecoderConfigurations** and **AddConfiguration** operations. 

- Device shall support removing an **Audio Output Configuration** or an **Audio Decoder Configuration** from a profile using the **RemoveConfiguration** operation. 

- Device shall deliver event notifications when an **Audio Output Configuration** or **Audio Decoder Configuration** is added or removed from a profile. 

## 8.13.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Media Profiles** using the **GetProfiles** operation. 

- Client shall be able to either: 

   - Add an **Audio Output Configuration** to a **Media Profile** using the **GetAudioOutputConfigurations** and **AddConfiguration** operations, or 

   - Create a media profile with an **Audio Output Configuration** according to **7.8.2** 

- Client shall be able to add an **Audio Decoder Configuration** to a **Media Profile** using the **GetAudioDecoderConfigurations** and **AddConfiguration** operations. 

www.onvif.org 

70 

ONVIF Profile T Specification v1.0 

## 8.13.3 Function list for devices 

|**Configuration of Audio Output Profile**<br>**Device CONDITIONAL**|**Configuration of Audio Output Profile**<br>**Device CONDITIONAL**|**Configuration of Audio Output Profile**<br>**Device CONDITIONAL**|**Configuration of Audio Output Profile**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetAudioOutputs|DeviceIO|M|
||GetAudioOutputConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||GetAudioDecoderConfigurations|Media 2|M|
||RemoveConfiguration|Media 2|M|
||tns1:Media/ProfileChanged|Event|M|



## 8.13.4 Function list for clients 

|**Configuration of Audio Output Profile**<br>**Client CONDITIONAL**|**Configuration of Audio Output Profile**<br>**Client CONDITIONAL**|**Configuration of Audio Output Profile**<br>**Client CONDITIONAL**|**Configuration of Audio Output Profile**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetProfiles|Media 2|M|
||GetAudioOutputs|DeviceIO|O|
||GetAudioOutputConfigurations|Media 2|M|
||AddConfiguration|Media 2|M|
||GetAudioDecoderConfigurations|Media 2|M|
||RemoveConfiguration|Media 2|O|
||tns1:Media/ProfileChanged|Event|O|



www.onvif.org 

71 

ONVIF Profile T Specification v1.0 

## 8.14 Focus control 

This section describes the operations related to focus control. 

## 8.14.1 Device requirements (if supported) 

- Device shall return available video sources in response to the **GetVideoSources** operation. 

- Device shall list available focus move options using the **GetMoveOptions** operation. 

- Device shall support focus movement using the **Move** and **Stop** operations. 

- Device shall report its current status using the **GetStatus** operation. 

## 8.14.2 Client requirements (if supported) 

- Client shall be able to control focus using the **GetMoveOptions** , **Move** and **Stop.** 

## 8.14.3 Function list for devices 

|**Focus Control**<br>**Device CONDITIONAL**|**Focus Control**<br>**Device CONDITIONAL**|**Focus Control**<br>**Device CONDITIONAL**|**Focus Control**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoSources|DeviceIO|M|
||GetMoveOptions|Imaging|M|
||Move|Imaging|M|
||Stop|Imaging|M|
||GetStatus|Imaging|M|



## 8.14.4 Function list for clients 

|**Focus Control**<br>**Client CONDITIONAL**|**Focus Control**<br>**Client CONDITIONAL**|**Focus Control**<br>**Client CONDITIONAL**|**Focus Control**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetVideoSources|DeviceIO|O|
||GetMoveOptions|Imaging|M|
||Move|Imaging|M|
||Stop|Imaging|M|
||GetStatus|Imaging|O|



www.onvif.org 

72 

ONVIF Profile T Specification v1.0 

## 8.15 Relay outputs 

This section describes the operations related to the control of Relay Outputs. 

## 8.15.1 Device requirements (if supported) 

- Device shall return available **Relay Outputs** in response to the **GetRelayOutputs** operation. 

- Device shall support modifying **Relay Output** settings in response to the **GetRelayOutputOptions** and **SetRelayOutputSettings** operations. 

- Device shall support control of the **Relay Output** state in response to the **SetRelayOutputState** operation. 

- Device shall generate **Relay Output** events according to the **Device IO Service Specification** . 

## 8.15.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Relay Outputs** using the **GetRelayOutputs** operation. 

- Client shall be able to control **Relay Output** state using the **SetRelayOutputState** operation. 

## 8.15.3 Function list for devices 

|**Relay Outputs**<br>**Device CONDITIONAL**|**Relay Outputs**<br>**Device CONDITIONAL**|**Relay Outputs**<br>**Device CONDITIONAL**|**Relay Outputs**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetRelayOutputs|DeviceIO|M|
||GetRelayOutputOptions|DeviceIO|M|
||SetRelayOutputSettings|DeviceIO|M|
||SetRelayOutputState|DeviceIO|M|
||tns1:Device/Trigger/Relay|Event|M|



www.onvif.org 

73 

ONVIF Profile T Specification v1.0 

## 8.15.4 Function list for clients 

|**Relay Outputs**<br>**Client CONDITIONAL**|**Relay Outputs**<br>**Client CONDITIONAL**|**Relay Outputs**<br>**Client CONDITIONAL**|**Relay Outputs**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetRelayOutputs|DeviceIO|M|
||GetRelayOutputOptions|DeviceIO|O|
||SetRelayOutputSettings|DeviceIO|O|
||SetRelayOutputState|DeviceIO|M|
||tns1:Device/Trigger/Relay|Event|O|



www.onvif.org 

74 

ONVIF Profile T Specification v1.0 

## 8.16 Digital inputs 

This section describes the operations related to the control of Digital Inputs connected to a device. 

## 8.16.1 Device requirements (if supported) 

- Device shall provide available **Digital Inputs** in response to the **GetDigitalInputs** operation. 

- Device shall support modifying **Digital Input** configurations in response to the **GetDigitalInputConfigurationOptions** and **SetDigitalInputConfigurations** operations. 

- Device shall generate **Digital Input** events according to the **Device IO Service Specification** . 

## 8.16.2 Client requirements (if supported) 

- Client shall be able to retrieve available **Digital Inputs** using the **GetDigitalInputs** operation. 

- Client shall monitor the state of the input pins with event topic **tns1:Device/Trigger/DigitalInput.** 

## 8.16.3 Function list for devices 

|**Digital Inputs**<br>**Device CONDITIONAL**|**Digital Inputs**<br>**Device CONDITIONAL**|**Digital Inputs**<br>**Device CONDITIONAL**|**Digital Inputs**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetDigitalInputs|DeviceIO|M|
||GetDigitalInputConfigurationOptions|DeviceIO|M|
||SetDigitalInputConfigurations|DeviceIO|M|
||tns1:Device/Trigger/DigitalInput|Event|M|



## 8.16.4 Function list for clients 

|**Digital Inputs**<br>**Client CONDITIONAL**|**Digital Inputs**<br>**Client CONDITIONAL**|**Digital Inputs**<br>**Client CONDITIONAL**|**Digital Inputs**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||GetDigitalInputs|DeviceIO|M|
||GetDigitalInputConfigurationOptions|DeviceIO|O|
||SetDigitalInputConfigurations|DeviceIO|O|
||tns1:Device/Trigger/DigitalInput|Event|M|



www.onvif.org 

75 

ONVIF Profile T Specification v1.0 

## 8.17 Auxiliary commands 

This section describes the operations related to auxiliary commands on a device. 

## 8.17.1 Device requirements (if supported) 

- Device shall support the **SendAuxiliaryCommand** operation as covered by the **Device Management** service. 

- Device shall return a list of supported auxiliary commands in the **Misc.AuxiliaryCommands** field in the response of the **GetServiceCapabilities** operation. 

## 8.17.2 Client requirements (if supported) 

- Client shall be able to execute auxiliary commands using the **SendAuxiliaryCommand** operation as covered by the **Device Management** service. 

## 8.17.3 Function list for devices 

|**Auxiliary Commands**<br>**Device CONDITIONAL**|**Auxiliary Commands**<br>**Device CONDITIONAL**|**Auxiliary Commands**<br>**Device CONDITIONAL**|**Auxiliary Commands**<br>**Device CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||SendAuxiliaryCommand|Device Management|M|
||tt:Wiper|On|Device Management|C|
||tt:Wiper|Off|Device Management|C|
||tt:Washer|On|Device Management|C|
||tt:Washer|Off|Device Management|C|
||tt:WashingProcedure|On|Device Management|C|
||tt:WashingProcedure|Off|Device Management|C|
||tt:IRLamp|On|Device Management|C|
||tt:IRLamp|Off|Device Management|C|
||tt:IRLamp|Auto|Device Management|C|



www.onvif.org 

76 

ONVIF Profile T Specification v1.0 

## 8.17.4 Function list for clients 

|**Auxiliary Commands**<br>**Client CONDITIONAL**|**Auxiliary Commands**<br>**Client CONDITIONAL**|**Auxiliary Commands**<br>**Client CONDITIONAL**|**Auxiliary Commands**<br>**Client CONDITIONAL**|
|---|---|---|---|
||**Function**|**Service**|**Requirement**|
||SendAuxiliaryCommand|Device Management|M|
||tt:Wiper|On|Device Management|M*|
||tt:Wiper|Off|Device Management||
||tt:Washer|On|Device Management||
||tt:Washer|Off|Device Management||
||tt:WashingProcedure|On|Device Management||
||tt:WashingProcedure|Off|Device Management||
||tt:IRLamp|On|Device Management||
||tt:IRLamp|Off|Device Management||
||tt:IRLamp|Auto|Device Management||



*Client shall support at least one of the listed commands. 

www.onvif.org 

77 

