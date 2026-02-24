#include "flutter_xr/app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

namespace flutter_xr {

namespace {

constexpr XrQuaternionf kRayAlignmentFromController = {0.0f, -0.70710677f, 0.0f, 0.70710677f};

float ApplyAxisDeadzone(float value) {
    const float magnitude = std::fabs(value);
    if (magnitude <= kScrollAxisDeadzone) {
        return 0.0f;
    }

    const float normalized = std::clamp((magnitude - kScrollAxisDeadzone) / (1.0f - kScrollAxisDeadzone), 0.0f, 1.0f);
    return std::copysign(normalized, value);
}

float MagnitudeSquared(const XrVector2f& value) {
    return value.x * value.x + value.y * value.y;
}

const PointerHitResult* SelectScrollHit(XrPath preferredHandPath,
                                        XrPath leftHandPath,
                                        const PointerHitResult& rightHit,
                                        const PointerHitResult& leftHit) {
    const PointerHitResult* preferred = (preferredHandPath == leftHandPath) ? &leftHit : &rightHit;
    const PointerHitResult* fallback = (preferred == &leftHit) ? &rightHit : &leftHit;

    if (preferred->onQuad) {
        return preferred;
    }
    if (fallback->onQuad) {
        return fallback;
    }
    return nullptr;
}

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
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/left", &leftHandPath_), "xrStringToPath(/user/hand/left)",
                    instance_);

    {
        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        std::strcpy(actionInfo.actionName, "pointer_pose");
        std::strcpy(actionInfo.localizedActionName, "Pointer Pose");
        const std::array<XrPath, 2> handSubactionPaths = {leftHandPath_, rightHandPath_};
        actionInfo.countSubactionPaths = static_cast<uint32_t>(handSubactionPaths.size());
        actionInfo.subactionPaths = handSubactionPaths.data();
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

    {
        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
        std::strcpy(actionInfo.actionName, "scroll_axis");
        std::strcpy(actionInfo.localizedActionName, "Scroll Axis");
        const std::array<XrPath, 2> handSubactionPaths = {leftHandPath_, rightHandPath_};
        actionInfo.countSubactionPaths = static_cast<uint32_t>(handSubactionPaths.size());
        actionInfo.subactionPaths = handSubactionPaths.data();
        ThrowIfXrFailed(xrCreateAction(inputActionSet_, &actionInfo, &scrollVectorAction_), "xrCreateAction(scrollAxis)",
                        instance_);
    }

    XrPath rightSelectClickPath = XR_NULL_PATH;
    XrPath rightTriggerValuePath = XR_NULL_PATH;
    XrPath rightAimPosePath = XR_NULL_PATH;
    XrPath leftAimPosePath = XR_NULL_PATH;
    XrPath rightGripPosePath = XR_NULL_PATH;
    XrPath leftGripPosePath = XR_NULL_PATH;
    XrPath rightThumbstickPath = XR_NULL_PATH;
    XrPath leftThumbstickPath = XR_NULL_PATH;
    XrPath rightTrackpadPath = XR_NULL_PATH;
    XrPath leftTrackpadPath = XR_NULL_PATH;

    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/select/click", &rightSelectClickPath),
                    "xrStringToPath(right select click)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/trigger/value", &rightTriggerValuePath),
                    "xrStringToPath(right trigger value)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/aim/pose", &rightAimPosePath),
                    "xrStringToPath(right aim pose)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/left/input/aim/pose", &leftAimPosePath),
                    "xrStringToPath(left aim pose)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/grip/pose", &rightGripPosePath),
                    "xrStringToPath(right grip pose)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/left/input/grip/pose", &leftGripPosePath),
                    "xrStringToPath(left grip pose)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/thumbstick", &rightThumbstickPath),
                    "xrStringToPath(right thumbstick)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/left/input/thumbstick", &leftThumbstickPath),
                    "xrStringToPath(left thumbstick)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/trackpad", &rightTrackpadPath),
                    "xrStringToPath(right trackpad)", instance_);
    ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/left/input/trackpad", &leftTrackpadPath),
                    "xrStringToPath(left trackpad)", instance_);

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

    SuggestBindings(khrSimpleInteractionProfile,
                    {{triggerValueAction_, rightSelectClickPath},
                     {pointerPoseAction_, rightGripPosePath},
                     {pointerPoseAction_, leftGripPosePath}});
    SuggestBindings(oculusTouchInteractionProfile,
                    {{triggerValueAction_, rightTriggerValuePath},
                     {pointerPoseAction_, rightAimPosePath},
                     {pointerPoseAction_, leftAimPosePath},
                     {scrollVectorAction_, rightThumbstickPath},
                     {scrollVectorAction_, leftThumbstickPath}});
    SuggestBindings(viveInteractionProfile,
                    {{triggerValueAction_, rightTriggerValuePath},
                     {pointerPoseAction_, rightGripPosePath},
                     {pointerPoseAction_, leftGripPosePath},
                     {scrollVectorAction_, rightTrackpadPath},
                     {scrollVectorAction_, leftTrackpadPath}});
    SuggestBindings(indexInteractionProfile,
                    {{triggerValueAction_, rightTriggerValuePath},
                     {pointerPoseAction_, rightGripPosePath},
                     {pointerPoseAction_, leftGripPosePath},
                     {scrollVectorAction_, rightThumbstickPath},
                     {scrollVectorAction_, leftThumbstickPath},
                     {scrollVectorAction_, rightTrackpadPath},
                     {scrollVectorAction_, leftTrackpadPath}});
    SuggestBindings(microsoftMotionInteractionProfile,
                    {{triggerValueAction_, rightTriggerValuePath},
                     {pointerPoseAction_, rightGripPosePath},
                     {pointerPoseAction_, leftGripPosePath},
                     {scrollVectorAction_, rightThumbstickPath},
                     {scrollVectorAction_, leftThumbstickPath},
                     {scrollVectorAction_, rightTrackpadPath},
                     {scrollVectorAction_, leftTrackpadPath}});

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

    actionSpaceInfo.subactionPath = leftHandPath_;
    ThrowIfXrFailed(xrCreateActionSpace(session_, &actionSpaceInfo, &leftPointerSpace_),
                    "xrCreateActionSpace(pointerLeft)", instance_);
}

PointerHitResult FlutterXrApp::QueryPointerHit(XrTime predictedDisplayTime, XrSpace pointerSpace, XrPath handPath) {
    PointerHitResult result;
    if (pointerSpace == XR_NULL_HANDLE || handPath == XR_NULL_PATH) {
        return result;
    }

    XrActionStateGetInfo poseGetInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    poseGetInfo.action = pointerPoseAction_;
    poseGetInfo.subactionPath = handPath;
    XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};
    ThrowIfXrFailed(xrGetActionStatePose(session_, &poseGetInfo, &poseState), "xrGetActionStatePose(pointerPose)",
                    instance_);
    if (poseState.isActive != XR_TRUE) {
        return result;
    }

    XrSpaceLocation pointerLocation{XR_TYPE_SPACE_LOCATION};
    const XrResult locateResult = xrLocateSpace(pointerSpace, appSpace_, predictedDisplayTime, &pointerLocation);
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

bool FlutterXrApp::SendFlutterScrollEvent(double xPixels, double yPixels, double deltaXPixels, double deltaYPixels) {
    if (flutterEngine_ == nullptr) {
        return false;
    }

    FlutterPointerEvent event{};
    event.struct_size = sizeof(event);
    event.phase = pointerDown_ ? kMove : kHover;
    event.timestamp = static_cast<size_t>(FlutterEngineGetCurrentTime());
    event.x = xPixels;
    event.y = yPixels;
    event.device = kPointerDeviceId;
    event.signal_kind = kFlutterPointerSignalKindScroll;
    event.scroll_delta_x = deltaXPixels;
    event.scroll_delta_y = deltaYPixels;
    event.device_kind = kFlutterPointerDeviceKindMouse;
    event.buttons = pointerDown_ ? kFlutterPointerButtonMousePrimary : 0;
    event.view_id = kFlutterViewId;

    const FlutterEngineResult result = FlutterEngineSendPointerEvent(flutterEngine_, &event, 1);
    if (result != kSuccess) {
        std::cerr << "[warn] FlutterEngineSendPointerEvent failed. signal=scroll"
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
        leftPointerRayVisible_ = false;
        return;
    }

    const XrActiveActionSet activeActionSet{inputActionSet_, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    ThrowIfXrFailed(xrSyncActions(session_, &syncInfo), "xrSyncActions", instance_);

    const PointerHitResult hit = QueryPointerHit(predictedDisplayTime, pointerSpace_, rightHandPath_);
    const PointerHitResult leftHit = QueryPointerHit(predictedDisplayTime, leftPointerSpace_, leftHandPath_);

    auto updateRayState = [&](const PointerHitResult& pointerHit, bool* visible, float* length, XrPosef* pose) {
        if (pointerHit.hasPose) {
            const float rayLength =
                pointerHit.onQuad
                    ? std::clamp(pointerHit.hitDistanceMeters, kPointerRayMinLengthMeters, kPointerRayFallbackLengthMeters)
                    : kPointerRayFallbackLengthMeters;
            *length = rayLength;
            pose->orientation = Multiply(pointerHit.pointerOrientation, kRayAlignmentFromController);
            pose->position = Add(pointerHit.rayOriginWorld, Scale(pointerHit.rayDirectionWorld, rayLength * 0.5f));
            *visible = true;
            return;
        }
        *visible = false;
    };

    updateRayState(hit, &pointerRayVisible_, &pointerRayLengthMeters_, &pointerRayPose_);
    updateRayState(leftHit, &leftPointerRayVisible_, &leftPointerRayLengthMeters_, &leftPointerRayPose_);

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

    if (scrollVectorAction_ == XR_NULL_HANDLE || flutterEngine_ == nullptr) {
        return;
    }

    XrActionStateGetInfo scrollGetInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    scrollGetInfo.action = scrollVectorAction_;

    scrollGetInfo.subactionPath = rightHandPath_;
    XrActionStateVector2f rightScrollState{XR_TYPE_ACTION_STATE_VECTOR2F};
    ThrowIfXrFailed(xrGetActionStateVector2f(session_, &scrollGetInfo, &rightScrollState),
                    "xrGetActionStateVector2f(scroll right)", instance_);

    scrollGetInfo.subactionPath = leftHandPath_;
    XrActionStateVector2f leftScrollState{XR_TYPE_ACTION_STATE_VECTOR2F};
    ThrowIfXrFailed(xrGetActionStateVector2f(session_, &scrollGetInfo, &leftScrollState),
                    "xrGetActionStateVector2f(scroll left)", instance_);

    const bool rightScrollActive = rightScrollState.isActive == XR_TRUE;
    const bool leftScrollActive = leftScrollState.isActive == XR_TRUE;

    XrVector2f scrollAxis{0.0f, 0.0f};
    XrPath scrollAxisHandPath = rightHandPath_;
    if (rightScrollActive && leftScrollActive) {
        if (MagnitudeSquared(leftScrollState.currentState) > MagnitudeSquared(rightScrollState.currentState)) {
            scrollAxis = leftScrollState.currentState;
            scrollAxisHandPath = leftHandPath_;
        } else {
            scrollAxis = rightScrollState.currentState;
            scrollAxisHandPath = rightHandPath_;
        }
    } else if (rightScrollActive) {
        scrollAxis = rightScrollState.currentState;
        scrollAxisHandPath = rightHandPath_;
    } else if (leftScrollActive) {
        scrollAxis = leftScrollState.currentState;
        scrollAxisHandPath = leftHandPath_;
    }

    const double scrollDeltaX = static_cast<double>(ApplyAxisDeadzone(scrollAxis.x)) * kScrollPixelsPerFrame;
    const double scrollDeltaY = -static_cast<double>(ApplyAxisDeadzone(scrollAxis.y)) * kScrollPixelsPerFrame;
    if (std::abs(scrollDeltaX) <= kScrollDeltaEpsilonPixels && std::abs(scrollDeltaY) <= kScrollDeltaEpsilonPixels) {
        return;
    }

    const PointerHitResult* scrollHit = SelectScrollHit(scrollAxisHandPath, leftHandPath_, hit, leftHit);
    double scrollX = lastPointerX_;
    double scrollY = lastPointerY_;
    if (scrollHit != nullptr) {
        scrollX = scrollHit->xPixels;
        scrollY = scrollHit->yPixels;
        EnsureFlutterPointerAdded(scrollX, scrollY);
        if (pointerAdded_ && !pointerDown_) {
            SendFlutterPointerEvent(kHover, scrollX, scrollY, 0);
        }
    }

    if (pointerAdded_) {
        SendFlutterScrollEvent(scrollX, scrollY, scrollDeltaX, scrollDeltaY);
    }
}

}  // namespace flutter_xr
