#pragma once
// PtzService — ONVIF PTZ Service (ver20), zoom-only node.
//
// Camera vật lý (xem docs/onvif-alvis/01-IMPLEMENTATION_PLAN.md Phase 5) có
// lens motorized zoom/focus thật (MGMT LensApiController) nhưng KHÔNG có
// pan/tilt hardware — vì vậy node chỉ khai báo AbsoluteZoomPositionSpace,
// không có bất kỳ Pan/Tilt space nào. Chỉ implement AbsoluteMove (position-based,
// khớp đúng MGMT ZoomValue contract) — KHÔNG implement ContinuousMove/RelativeMove
// vì MGMT không có khái niệm velocity (nguyên tắc README #5: không quảng bá
// capability mà backend thật không thực hiện được).
//
// 1 PTZNode + 1 PTZConfiguration cho MỖI VideoSourceToken thật (đọc động từ
// backend_->getProfiles(), giống ImagingService — không hardcode "0"/"1").
// Operations dùng ProfileToken (chuẩn ONVIF) — PtzService tự resolve
// ProfileToken -> sourceToken rồi gọi backend_ bằng sourceToken (khớp cách
// ImagingService dùng VideoSourceToken làm khóa thống nhất mock+real).

#include "soapPTZBindingService.h"
#include "services/DeviceService.h"     // ServiceConfig
#include "interface/ICameraBackend.h"
#include <memory>
#include <string>

class PtzService : public PTZBindingService {
public:
    PtzService(struct soap* soap,
               const ServiceConfig& cfg,
               std::shared_ptr<ICameraBackend> backend);
    ~PtzService() = default;

    PTZBindingService* copy() override;

    int GetServiceCapabilities(_tptz__GetServiceCapabilities *req,
                               _tptz__GetServiceCapabilitiesResponse &resp) override;
    int GetNodes(_tptz__GetNodes *req, _tptz__GetNodesResponse &resp) override;
    int GetNode(_tptz__GetNode *req, _tptz__GetNodeResponse &resp) override;
    int GetConfigurations(_tptz__GetConfigurations *req,
                          _tptz__GetConfigurationsResponse &resp) override;
    int GetConfiguration(_tptz__GetConfiguration *req,
                        _tptz__GetConfigurationResponse &resp) override;
    int GetConfigurationOptions(_tptz__GetConfigurationOptions *req,
                                _tptz__GetConfigurationOptionsResponse &resp) override;
    int SetConfiguration(_tptz__SetConfiguration *req,
                        _tptz__SetConfigurationResponse &resp) override;
    int GetCompatibleConfigurations(_tptz__GetCompatibleConfigurations *req,
                                    _tptz__GetCompatibleConfigurationsResponse &resp) override;
    int AbsoluteMove(_tptz__AbsoluteMove *req, _tptz__AbsoluteMoveResponse &resp) override;
    int Stop(_tptz__Stop *req, _tptz__StopResponse &resp) override;
    int GetStatus(_tptz__GetStatus *req, _tptz__GetStatusResponse &resp) override;

    // Không hỗ trợ (không có hardware/backend tương ứng) — luôn trả
    // ter:ActionNotSupported. Xem lời giải thích ở đầu file.
    int GetPresets(_tptz__GetPresets *req, _tptz__GetPresetsResponse &resp) override;
    int SetPreset(_tptz__SetPreset *req, _tptz__SetPresetResponse &resp) override;
    int RemovePreset(_tptz__RemovePreset *req, _tptz__RemovePresetResponse &resp) override;
    int GotoPreset(_tptz__GotoPreset *req, _tptz__GotoPresetResponse &resp) override;
    int ContinuousMove(_tptz__ContinuousMove *req, _tptz__ContinuousMoveResponse &resp) override;
    int RelativeMove(_tptz__RelativeMove *req, _tptz__RelativeMoveResponse &resp) override;
    int SendAuxiliaryCommand(_tptz__SendAuxiliaryCommand *req,
                             _tptz__SendAuxiliaryCommandResponse &resp) override;
    int GeoMove(_tptz__GeoMove *req, _tptz__GeoMoveResponse &resp) override;
    int GotoHomePosition(_tptz__GotoHomePosition *req,
                        _tptz__GotoHomePositionResponse &resp) override;
    int SetHomePosition(_tptz__SetHomePosition *req,
                       _tptz__SetHomePositionResponse &resp) override;
    int GetPresetTours(_tptz__GetPresetTours *req, _tptz__GetPresetToursResponse &resp) override;
    int GetPresetTour(_tptz__GetPresetTour *req, _tptz__GetPresetTourResponse &resp) override;
    int GetPresetTourOptions(_tptz__GetPresetTourOptions *req,
                            _tptz__GetPresetTourOptionsResponse &resp) override;
    int CreatePresetTour(_tptz__CreatePresetTour *req,
                        _tptz__CreatePresetTourResponse &resp) override;
    int ModifyPresetTour(_tptz__ModifyPresetTour *req,
                       _tptz__ModifyPresetTourResponse &resp) override;
    int OperatePresetTour(_tptz__OperatePresetTour *req,
                        _tptz__OperatePresetTourResponse &resp) override;
    int RemovePresetTour(_tptz__RemovePresetTour *req,
                       _tptz__RemovePresetTourResponse &resp) override;
    int MoveAndStartTracking(_tptz__MoveAndStartTracking *req,
                            _tptz__MoveAndStartTrackingResponse &resp) override;

private:
    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;

    static std::string nodeToken(const std::string& sourceToken);
    static std::string configToken(const std::string& sourceToken);
    // "ptz_node_X" / "ptz_config_X" -> sourceToken "X" gốc, "" nếu không khớp prefix.
    static std::string sourceTokenFromNode(const std::string& tok);
    static std::string sourceTokenFromConfig(const std::string& tok);
    // ProfileToken (ONVIF media profile) -> sourceToken thật, "" nếu không tìm thấy.
    std::string resolveProfileToSource(const std::string& profileToken) const;

    int sendXml(const std::string& xml);
    int sendFault(const std::string& faultXml);
};
