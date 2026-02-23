#include "flutter_xr/app.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace flutter_xr {

namespace {

constexpr XrQuaternionf kRayAlignmentFromController = {0.0f, -0.70710677f, 0.0f, 0.70710677f};

}  // namespace

void FlutterXrApp::SuggestBindings(XrPath interactionProfile, const std::vector<XrActionSuggestedBinding>& bindings) {
    XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestedBindings.interactionProfile = interactionProfile;
    suggestedBindings.suggestedBindings = bindings.data();
    suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    ThrowIfXrFailed(xrSuggestInteractionProfileBindings(instance_, &suggestedBindings),
                    "xrSuggestInteractionProfileBindings", instance_);
}

void FlutterXrApp::InitializeInputActions() {
    XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(actionSetInfo.actionSetName, "flutter_input");
    std::strcpy(actionSetInfo.localizedActionSetName, "Flutter Input");
    actionSetInfo.priority = 0;
    ThrowIfXrFailed(xrCreateActionSet(instance_, &actionSetInfo, &inputActionSet_), "xrCreateActionSet", instance_);

    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right", &rightHandPath_), "xrStringToPath(/user/hand/right)",
                    instance_);

    {
        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        std::strcpy(actionInfo.actionName, "pointer_pose");
        std::strcpy(actionInfo.localizedActionName, "Pointer Pose");
        actionInfo.countSubactionPaths = 1;
        actionInfo.subactionPaths = &rightHandPath_;
        ThrowIfXrFailed(xrCreateAction(inputActionSet_, &actionInfo, &pointerPoseAction_), "xrCreateAction(pointerPose)",
                        instance_);
    }

    {
        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
        std::strcpy(actionInfo.actionName, "trigger_value");
        std::strcpy(actionInfo.localizedActionName, "Trigger Value");
        actionInfo.countSubactionPaths = 1;
        actionInfo.subactionPaths = &rightHandPath_;
        ThrowIfXrFailed(xrCreateAction(inputActionSet_, &actionInfo, &triggerValueAction_), "xrCreateAction(triggerValue)",
                        instance_);
    }

    XrPath rightSelectClickPath = XR_NULL_PATH;
    XrPath rightTriggerValuePath = XR_NULL_PATH;
    XrPath rightAimPosePath = XR_NULL_PATH;
    XrPath rightGripPosePath = XR_NULL_PATH;

    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/select/click", &rightSelectClickPath),
                    "xrStringToPath(right select click)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/trigger/value", &rightTriggerValuePath),
                    "xrStringToPath(right trigger value)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/aim/pose", &rightAimPosePath),
                    "xrStringToPath(right aim pose)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/grip/pose", &rightGripPosePath),
                    "xrStringToPath(right grip pose)", instance_);

    XrPath khrSimpleInteractionProfile = XR_NULL_PATH;
    XrPath oculusTouchInteractionProfile = XR_NULL_PATH;
    XrPath viveInteractionProfile = XR_NULL_PATH;
    XrPath indexInteractionProfile = XR_NULL_PATH;
    XrPath microsoftMotionInteractionProfile = XR_NULL_PATH;

    ThrowIfXrFailed(xrStringToPath(instance_, "/interaction_profiles/khr/simple_controller", &khrSimpleInteractionProfile),
                    "xrStringToPath(khr simple profile)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/interaction_profiles/oculus/touch_controller", &oculusTouchInteractionProfile),
                    "xrStringToPath(oculus touch profile)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/interaction_profiles/htc/vive_controller", &viveInteractionProfile),
                    "xrStringToPath(vive profile)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/interaction_profiles/valve/index_controller", &indexInteractionProfile),
                    "xrStringToPath(index profile)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/interaction_profiles/microsoft/motion_controller",
                                   &microsoftMotionInteractionProfile),
                    "xrStringToPath(microsoft motion profile)", instance_);

    SuggestBindings(khrSimpleInteractionProfile, {{triggerValueAction_, rightSelectClickPath}, {pointerPoseAction_, rightGripPosePath}});
    SuggestBindings(oculusTouchInteractionProfile, {{triggerValueAction_, rightTriggerValuePath}, {pointerPoseAction_, rightAimPosePath}});
    SuggestBindings(viveInteractionProfile, {{triggerValueAction_, rightTriggerValuePath}, {pointerPoseAction_, rightGripPosePath}});
    SuggestBindings(indexInteractionProfile, {{triggerValueAction_, rightTriggerValuePath}, {pointerPoseAction_, rightGripPosePath}});
    SuggestBindings(microsoftMotionInteractionProfile,
                    {{triggerValueAction_, rightTriggerValuePath}, {pointerPoseAction_, rightGripPosePath}});

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &inputActionSet_;
    ThrowIfXrFailed(xrAttachSessionActionSets(session_, &attachInfo), "xrAttachSessionActionSets", instance_);

    XrActionSpaceCreateInfo actionSpaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    actionSpaceInfo.action = pointerPoseAction_;
    actionSpaceInfo.subactionPath = rightHandPath_;
    actionSpaceInfo.poseInActionSpace.orientation.w = 1.0f;
    ThrowIfXrFailed(xrCreateActionSpace(session_, &actionSpaceInfo, &pointerSpace_), "xrCreateActionSpace(pointer)",
                    instance_);
}

PointerHitResult FlutterXrApp::QueryPointerHit(XrTime predictedDisplayTime) {
    PointerHitResult result;
    if (pointerSpace_ == XR_NULL_HANDLE) {
        return result;
    }

    XrActionStateGetInfo poseGetInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    poseGetInfo.action = pointerPoseAction_;
    poseGetInfo.subactionPath = rightHandPath_;
    XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};
    ThrowIfXrFailed(xrGetActionStatePose(session_, &poseGetInfo, &poseState), "xrGetActionStatePose(pointerPose)",
                    instance_);
    if (poseState.isActive != XR_TRUE) {
        return result;
    }

    XrSpaceLocation pointerLocation{XR_TYPE_SPACE_LOCATION};
    const XrResult locateResult = xrLocateSpace(pointerSpace_, appSpace_, predictedDisplayTime, &pointerLocation);
    if (XR_FAILED(locateResult)) {
        return result;
    }

    constexpr XrSpaceLocationFlags kRequiredFlags =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((pointerLocation.locationFlags & kRequiredFlags) != kRequiredFlags) {
        return result;
    }

    const XrVector3f rayForward = RotateVector(pointerLocation.pose.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
    result.hasPose = true;
    result.rayOriginWorld = pointerLocation.pose.position;
    result.rayDirectionWorld = Normalize(rayForward);
    result.pointerOrientation = pointerLocation.pose.orientation;

    const XrPosef quadPose = MakeQuadPose();

    double u = 0.0;
    double v = 0.0;
    if (!IntersectRayWithQuad(pointerLocation.pose.position, result.rayDirectionWorld, quadPose, kQuadWidthMeters, kQuadHeightMeters,
                              &result.hitDistanceMeters,
                              &u, &v)) {
        return result;
    }

    result.onQuad = true;
    result.xPixels = std::clamp(u * static_cast<double>(kFlutterSurfaceWidth), 0.0, static_cast<double>(kFlutterSurfaceWidth - 1));
    result.yPixels = std::clamp(v * static_cast<double>(kFlutterSurfaceHeight), 0.0, static_cast<double>(kFlutterSurfaceHeight - 1));
    return result;
}

bool FlutterXrApp::SendFlutterPointerEvent(FlutterPointerPhase phase, double xPixels, double yPixels, int64_t buttons) {
    if (flutterEngine_ == nullptr) {
        return false;
    }

    FlutterPointerEvent event{};
    event.struct_size = sizeof(event);
    event.phase = phase;
    event.timestamp = static_cast<size_t>(FlutterEngineGetCurrentTime());
    event.x = xPixels;
    event.y = yPixels;
    event.device = kPointerDeviceId;
    event.signal_kind = kFlutterPointerSignalKindNone;
    event.device_kind = kFlutterPointerDeviceKindMouse;
    event.buttons = buttons;
    event.view_id = kFlutterViewId;

    const FlutterEngineResult result = FlutterEngineSendPointerEvent(flutterEngine_, &event, 1);
    if (result != kSuccess) {
        std::cerr << "[warn] FlutterEngineSendPointerEvent failed. phase=" << static_cast<int32_t>(phase)
                  << " result=" << static_cast<int32_t>(result) << "\n";
        return false;
    }

    lastPointerX_ = xPixels;
    lastPointerY_ = yPixels;
    return true;
}

void FlutterXrApp::EnsureFlutterPointerAdded(double xPixels, double yPixels) {
    if (pointerAdded_) {
        return;
    }
    if (SendFlutterPointerEvent(kAdd, xPixels, yPixels, 0)) {
        pointerAdded_ = true;
    }
}

void FlutterXrApp::PollInput(XrTime predictedDisplayTime) {
    if (inputActionSet_ == XR_NULL_HANDLE) {
        return;
    }

    if (sessionState_ != XR_SESSION_STATE_FOCUSED) {
        if (pointerDown_) {
            SendFlutterPointerEvent(kUp, lastPointerX_, lastPointerY_, 0);
            pointerDown_ = false;
        }
        triggerPressed_ = false;
        pointerRayVisible_ = false;
        return;
    }

    const XrActiveActionSet activeActionSet{inputActionSet_, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    ThrowIfXrFailed(xrSyncActions(session_, &syncInfo), "xrSyncActions", instance_);

    const PointerHitResult hit = QueryPointerHit(predictedDisplayTime);
    if (hit.hasPose) {
        const float rayLength = hit.onQuad
                                    ? std::clamp(hit.hitDistanceMeters, kPointerRayMinLengthMeters, kPointerRayFallbackLengthMeters)
                                    : kPointerRayFallbackLengthMeters;
        pointerRayLengthMeters_ = rayLength;
        pointerRayPose_.orientation = Multiply(hit.pointerOrientation, kRayAlignmentFromController);
        pointerRayPose_.position = Add(hit.rayOriginWorld, Scale(hit.rayDirectionWorld, rayLength * 0.5f));
        pointerRayVisible_ = true;
    } else {
        pointerRayVisible_ = false;
    }

    XrActionStateGetInfo triggerGetInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    triggerGetInfo.action = triggerValueAction_;
    triggerGetInfo.subactionPath = rightHandPath_;
    XrActionStateFloat triggerState{XR_TYPE_ACTION_STATE_FLOAT};
    ThrowIfXrFailed(xrGetActionStateFloat(session_, &triggerGetInfo, &triggerState), "xrGetActionStateFloat(trigger)",
                    instance_);

    const bool inputActive = (triggerState.isActive == XR_TRUE);
    const float triggerValue = inputActive ? triggerState.currentState : 0.0f;
    const bool pressedNow =
        triggerPressed_ ? (triggerValue >= kTriggerReleaseThreshold) : (triggerValue >= kTriggerPressThreshold);

    if (pressedNow && !triggerPressed_) {
        if (hit.onQuad && flutterEngine_ != nullptr) {
            EnsureFlutterPointerAdded(hit.xPixels, hit.yPixels);
            if (pointerAdded_ && SendFlutterPointerEvent(kDown, hit.xPixels, hit.yPixels, kFlutterPointerButtonMousePrimary)) {
                pointerDown_ = true;
            }
        }
    } else if ((!pressedNow || !inputActive) && triggerPressed_) {
        if (pointerDown_ && flutterEngine_ != nullptr) {
            const double upX = hit.onQuad ? hit.xPixels : lastPointerX_;
            const double upY = hit.onQuad ? hit.yPixels : lastPointerY_;
            SendFlutterPointerEvent(kUp, upX, upY, 0);
            pointerDown_ = false;
        }
    }

    triggerPressed_ = inputActive && pressedNow;
}

}  // namespace flutter_xr
