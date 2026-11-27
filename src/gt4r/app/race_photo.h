#pragma once

// The photo pause over a replay (PhotoPause 0x3C3C20: the Replay Theater's Photo Mode, the photo drive's replays and its B-spec
// run): the race held still, a camera of the list turned and zoomed by the pad (race/photo_pause.h), the finder with the movie
// menu/common/racephoto.strb over it, and the photo of 32 pictures; then the project photo_save over the still race.
// docs/formats/photo.md "The photo pause".

#include "gt4r/app/app.h"
#include "gt4r/app/photo_exposure.h"
#include "gt4r/app/race_world.h"
#include "gt4r/photo/film.h"
#include "gt4r/race/photo_pause.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace gt4r {

class RaceHud;
namespace race {
struct StrobeMovie;
class StrobePlayer;
} // namespace race

class RacePhotoPause {
public:
    // What it asks of the race.
    struct Race {
        // The race drawn over `rect` from `pose` with a lens (none: the plain one) and a grade, its cars `ahead` of a step on from
        // the ones shown (0 .. 1: the shutter's time).
        std::function<void(Ui& ui, const SDL_FRect& rect, const race::ViewPose& pose, const RaceWorld::Lens* lens, const gt3r::render::DrawOptions::PostGrade& grade,
                           float ahead)>
            draw;
        race::photo::ShotCamera::World world;
        // Where the followed car is in a picture through `view` with `px` / `py` the projection's scales (the photo's frame, -1 .. 1; none:
        // not in it).
        std::function<std::optional<menu::JpegPicture::Focus>(const gt3r::render::Mat4& view, float px, float py)> car_frame;
        // Where course space is carried by the followed car over a step (its body at the next step's state times the inverse of
        // the shown one's); none without a next step.
        std::function<std::optional<gt3r::render::Mat4>()> car_motion;
    };

    RacePhotoPause(App& app, RaceHud* hud, Race race, const race::photo::ShotScene& scene, const menu::GameState* game);
    ~RacePhotoPause();

    // A frame of the pause. `m`: the menus' keys; `pad`: the pad itself (its sticks).
    void update(const MenuInput& m, const gt3r::PadState& pad, double dt);
    void render(Ui& ui);

    // The photo is taken and waits for its page (photo_save), then for back_from_saving().
    bool wants_saving() const { return state_ == State::Saved; }
    std::shared_ptr<menu::PhotoPicture> picture() const { return picture_; }
    void back_from_saving();
    // Select: the pause is over (the replay goes on).
    bool closed() const { return state_ == State::Closed; }

    // The photo just taken as a film's settings and cameras (0x334FC8: FILM, and a PCAM a frame: the step shown's, the next's);
    // the race adds its cars.
    photo::film::Film film() const;
    const race::photo::ShotCamera& camera() const { return camera_; }
    size_t shot_count() const { return shots_.size(); }
    size_t shot_index() const { return index_; }
    int filter() const { return filter_; }

private:
    enum class State { Finder, Exposing, Saved, Closed };
    void step(const race::photo::PadButtons& b);
    void open_movie();
    race::ViewPose pose(float aspect, float ahead = 0) const;
    gt3r::render::DrawOptions::PostGrade grade() const;
    photo::Correction correction() const;
    void expose(Ui& ui);
    void play(const char* sound);

    App& app_;
    RaceHud* hud_;
    Race race_;
    State state_ = State::Finder;
    std::vector<race::photo::Shot> shots_;
    size_t index_ = 0;
    race::photo::ShotCamera camera_;
    int filter_ = 0, blink_ = -1, leaving_ = 0;
    float saturation_option_ = 0;
    bool wide_ = false;
    bool focus_told_ = false; // ISFOCUSTRIGGER: the focus taken this step
    double time_ = 0;
    int repeat_dir_[2] = {}; double repeat_time_[2] = {};
    std::shared_ptr<const race::StrobeMovie> movie_;
    std::unique_ptr<race::StrobePlayer> player_;
    PhotoExposure exposure_;
    std::vector<photo::Sample> samples_;
    std::shared_ptr<menu::PhotoPicture> picture_;
    int quality_ = 50;
    // L1 + Cross recomposed the camera: during the photo it goes from where it was to where it is (the camera's views between its
    // last step's and this one's, CameraVariable::vf16 0x3796C8 with the picture's time).
    struct Pan {
        race::photo::Vec3 eye;
        float yaw = 0, pitch = 0, fov = 0;
    };
    std::optional<Pan> pan_from_;
};

} // namespace gt4r
