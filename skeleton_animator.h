#ifndef SKELETON_ANIMATOR_H
#define SKELETON_ANIMATOR_H

#include "core/math/transform.h"
#include "core/os/dir_access.h"
#include "core/os/file_access.h"
#include "scene/animation/animation_player.h"
#include "scene/3d/skeleton.h"
#include "scene/resources/packed_scene.h"


class SkeletonAnimator : public Node {
	GDCLASS(SkeletonAnimator, Node);

public:

	// -------------------------------------------------------------------------
	// Enums
	// -------------------------------------------------------------------------

	enum AnimationApplyMode {
		ANIMATION_APPLY_MODE_ADDITIVE,
		ANIMATION_APPLY_MODE_OVERRIDE,
		ANIMATION_APPLY_MODE_OVERRIDE_ROTATION,
	};

	enum AnimationState {
		ANIMATION_STATE_NONE,
		ANIMATION_STATE_STARTING,
		ANIMATION_STATE_LOOPING,
		ANIMATION_STATE_EXITING,
		ANIMATION_STATE_EXITED,
	};

	// -------------------------------------------------------------------------
	// Getters / Setters
	// -------------------------------------------------------------------------

	void					set_target_skeleton_path(const NodePath &p_path);
	NodePath				get_target_skeleton_path() const;

	void					set_animation(Ref<Animation> p_animation, bool p_duplicate = true);
	Ref<Animation>			get_animation() const;

	void					set_apply_mode(AnimationApplyMode p_mode);
	AnimationApplyMode		get_apply_mode() const;

	void					set_position(float p_position);
	float					get_position() const;

	void					set_multiplier(float p_multiplier);
	float					get_multiplier() const;

	void					set_mirror(bool p_mirror);
	bool					get_mirror() const;

	void					set_fade(bool p_fade);
	bool					get_fade() const;

	// fade_ratio: fraction of animation length used for fade-in/out.
	// Range [0.0, 1.0]. Default 0.25.
	void					set_fade_ratio(float p_ratio);
	float					get_fade_ratio() const;

	void					set_playing_speed(float p_speed);
	float					get_playing_speed() const;

	void					set_loop_count(int p_loop);
	int						get_loop_count() const;

	void					set_bone_filter(PoolStringArray p_bones);
	PoolStringArray			get_bone_filter() const;

	// ctrl_bone_name: name of the special control bone that encodes loop points
	// as Y-axis keyframe values. Default "animCtrl".
	void					set_ctrl_bone_name(const String &p_name);
	String					get_ctrl_bone_name() const;

	// ctrl_bone_fps: frames-per-second used when snapping loop-point times to
	// frame boundaries. Default 24.
	void					set_ctrl_bone_fps(float p_fps);
	float					get_ctrl_bone_fps() const;

	// ctrl_bone_tolerance: rounding tolerance applied to Y values when reading
	// the control bone. Default 0.1 (one decimal place).
	void					set_ctrl_bone_tolerance(float p_tolerance);
	float					get_ctrl_bone_tolerance() const;

	// -------------------------------------------------------------------------
	// State queries
	// -------------------------------------------------------------------------

	AnimationState			get_state() const;

	bool					is_playing() const;
	bool					is_middle() const;
	bool					is_looping() const;
	bool					has_ended() const;
	bool					is_exiting() const;

	bool					is_using_bone(const String &p_bone) const;
	int						get_bone_uses(const String &p_bone) const;

	float					get_animation_looped_length() const;
	String					get_animation_name() const;

	float					get_loop_start() const;
	float					get_loop_end() const;

	// -------------------------------------------------------------------------
	// Playback control
	// -------------------------------------------------------------------------

	void					play(float p_time = 0.0f, int p_loop = 1, float p_speed = 1.0f);
	void					stop(bool p_immediate);

	// Called each frame externally (or from _process if auto_process is true).
	void					process_animation(float p_delta);

	// Can be called directly to apply any animation at any position.
	void					apply_animation(
								AnimationApplyMode p_mode,
								Ref<Animation> p_animation,
								float p_position,
								float p_multiplier,
								PoolStringArray p_bone_filter,
								bool p_mirror = false);

	// When true, process_animation is called automatically from _process.
	void					set_auto_process(bool p_enable);
	bool					get_auto_process() const;

	// save_state / load_state allow external systems to snapshot and restore
	// mid-animation state (e.g. for cutscene rewind or network sync).
	Dictionary				save_state() const;
	void					load_state(Dictionary p_state);

	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	SkeletonAnimator();
	~SkeletonAnimator();

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:

	// ---- skeleton target ----
	NodePath				_target_skeleton_path;
	ObjectID				_target_skeleton_id = 0;
	Dictionary				_bone_id_mapping;
	Dictionary				_bone_uses;

	// ---- animation resource ----
	Ref<Animation>			_animation;
	bool					_initialized = false; // true once play() has been called at least once

	// ---- playback state ----
	AnimationState			_state    = ANIMATION_STATE_NONE;
	bool					_playing  = false;
	float					_position = 0.0f;
	float					_speed    = 1.0f;

	// ---- loop region (seconds, parsed from ctrl bone) ----
	float					_loop_start = 0.0f;
	float					_loop_end   = 0.0f;
	int						_loop_count = 0;

	// ---- fade ----
	bool					_fade             = false;
	float					_fade_ratio       = 0.25f; // fraction of anim length
	float					_fade_position    = 0.0f;  // tracks progress during short-end fade
	float					_fade_multiplier  = 1.0f;

	// ---- per-apply settings ----
	AnimationApplyMode		_apply_mode = ANIMATION_APPLY_MODE_OVERRIDE;
	float					_multiplier = 1.0f;
	bool					_mirror     = false;
	PoolStringArray			_bone_filter;

	// ---- ctrl bone config ----
	String					_ctrl_bone_name      = "animCtrl";
	float					_ctrl_bone_fps        = 24.0f;
	float					_ctrl_bone_tolerance  = 0.1f; // Y-value rounding tolerance

	// ---- misc ----
	bool					_auto_process   = false;
	bool					_pending_stop   = false;

	// ---- internal helpers ----
	Skeleton *				_get_skeleton() const;
	void					_rebuild_bone_map();
	void					_parse_ctrl_bone();
	void					_do_stop_immediate();
};

VARIANT_ENUM_CAST(SkeletonAnimator::AnimationState);
VARIANT_ENUM_CAST(SkeletonAnimator::AnimationApplyMode);

#endif // SKELETON_ANIMATOR_H
