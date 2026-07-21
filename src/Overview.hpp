#pragma once
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprutils/animation/AnimationConfig.hpp>

class CHyprspaceWidget {

    bool active = false;

    int64_t ownerID;

    Hyprutils::Animation::SAnimationPropertyConfig curAnimationConfig;
    SP<Hyprutils::Animation::SAnimationPropertyConfig> m_animationConfig;

    // for checking mouse hover for workspace drag and move
    // modified on draw call, accessed on mouse click and release
    std::vector<std::tuple<WORKSPACEID, CBox>> workspaceBoxes;

    // for storing the fullscreen state of windows prior to overview activation (which unfullscreens all windows)
    std::vector<std::tuple<PHLWINDOWREF, Fullscreen::SFullscreenMode>> prevFullscreen;

    // for storing the layer alpha values prior to overview activation (which sets all panel to transparent when configured)
    std::vector<std::tuple<PHLLS, float>> oLayerAlpha;

    // for click-to-exit
    std::chrono::high_resolution_clock::time_point lastPressedTime = std::chrono::high_resolution_clock::now();

    bool swiping = false;
    // whether if the panel is active before the current swiping event
    bool activeBeforeSwipe = false;
    double avgSwipeSpeed = 0.;
    // number of swiping speed frames recorded
    int swipePoints = 0;
    // Swipe travel in monitor-scaled pixels, where 0 is hidden and panelTravel() is fully shown.
    double curSwipeOffset = 0.;

    PHLANIMVAR<float> workspaceScrollOffset;

    void restoreHiddenLayers();
    void restoreFullscreenWindows();
    void releaseAnimations();
    bool animationsOk() const;
    void resetAnimationState(PHLMONITOR owner);

public:

    // for slide-in animation and swiping
    PHLANIMVAR<float> curYOffset;

    CHyprspaceWidget(uint64_t);
    ~CHyprspaceWidget();

    PHLMONITOR getOwner();
    bool isActive();

    void show();
    void hide();

    void updateConfig();
    void cleanup(PHLMONITOR owner = nullptr);

    // should be called active or not
    void draw();

    // reserves area on owner monitor
    void updateLayout();

    bool buttonEvent(bool, Vector2D coords);
    bool axisEvent(double, wl_pointer_axis axis, Vector2D coords);

    bool isSwiping();

    bool beginSwipe(IPointer::SSwipeBeginEvent);
    bool updateSwipe(IPointer::SSwipeUpdateEvent);
    bool endSwipe(IPointer::SSwipeEndEvent);

};
