#include "skeleton_animator.h"
#include "core/method_bind_ext.gen.inc"
#include "core/print_string.h"


// =============================================================================
//  Internal helpers
// =============================================================================

Skeleton *SkeletonAnimator::_get_skeleton() const {
	if (_target_skeleton_id == 0)
		return nullptr;
	Object *obj = ObjectDB::get_instance(_target_skeleton_id);
	return Object::cast_to<Skeleton>(obj);
}

void SkeletonAnimator::_rebuild_bone_map() {
	_bone_id_mapping.clear();
	Skeleton *sk = _get_skeleton();
	if (!sk)
		return;
	for (int i = 0; i < sk->get_bone_count(); i++) {
		_bone_id_mapping[sk->get_bone_name(i)] = i;
	}
}

// Reads the control bone track to extract loop_start and loop_end times.
//
// Convention (unchanged from original):
//   Key 0  – Y == 0.0  (animation start marker, optional)
//   Key 1  – Y == 1.0  → loop_start time
//   Key 2  – Y != 1.0  → loop_end time
//
// Times are snapped to the nearest frame boundary using _ctrl_bone_fps.
// Y values are compared with a tolerance of _ctrl_bone_tolerance.
void SkeletonAnimator::_parse_ctrl_bone() {
    _loop_start = 0.0f;
    _loop_end   = _animation->get_length();

    if (_ctrl_bone_name.empty())
        return;

    for (int tk = 0; tk < _animation->get_track_count(); tk++) {
        if (_animation->track_get_type(tk) != Animation::TYPE_TRANSFORM)
            continue;

        NodePath path = _animation->track_get_path(tk);
        if (path.get_subname_count() != 1)
            continue;
        if (path.get_subname(0) != _ctrl_bone_name)
            continue;

        int key_count = _animation->track_get_key_count(tk);
        bool in_loop  = false;

        for (int k = 0; k < key_count; k++) {
            Dictionary frame = _animation->track_get_key_value(tk, k);
            Vector3 loc = frame.get("location", Vector3());
            float t     = _animation->track_get_key_time(tk, k);
            float snapped = ceilf(t * _ctrl_bone_fps) / _ctrl_bone_fps;
            bool at_one = Math::is_equal_approx(loc.y, 1.0f, _ctrl_bone_tolerance);

            if (!in_loop && at_one) {
                _loop_start = snapped;
                in_loop     = true;
            } else if (in_loop && !at_one) {
                _loop_end = snapped;
                break;
            }
        }

        if (!in_loop) {
            WARN_PRINT("SkeletonAnimator: ctrl bone '" + _ctrl_bone_name +
                       "' found but no key with Y=1.0 detected. Loop points not set.");
        }
        break;
    }

    if (_loop_start >= _loop_end) {
        WARN_PRINT("SkeletonAnimator: loop_start (" + rtos(_loop_start) +
                   ") >= loop_end (" + rtos(_loop_end) + "). Resetting to full length.");
        _loop_start = 0.0f;
        _loop_end   = _animation->get_length();
    }
}

void SkeletonAnimator::_do_stop_immediate() {
	float current_speed = _speed;
	String anim_name    = get_animation_name();

	_speed           = 1.0f;
	_fade_multiplier = 0.0f;
	_position        = 0.0f;
	_state           = ANIMATION_STATE_EXITED;
	_playing         = false;
	_pending_stop    = false;
	_initialized     = false;

	emit_signal("animation_change", "", 0, -1.0f, current_speed);
	emit_signal("animation_end", anim_name);

	_animation.unref();
}


// =============================================================================
//  Lifecycle
// =============================================================================

void SkeletonAnimator::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!_target_skeleton_path.is_empty()) {
				set_target_skeleton_path(_target_skeleton_path);
			}
		} break;

		case NOTIFICATION_PROCESS: {
			if (_auto_process) {
				process_animation(get_process_delta_time());
			}
		} break;

		case NOTIFICATION_PARENTED:
		case NOTIFICATION_UNPARENTED: {
			// Re-resolve the skeleton pointer whenever the node moves in the tree.
			if (!_target_skeleton_path.is_empty() && is_inside_tree()) {
				set_target_skeleton_path(_target_skeleton_path);
			} else {
				_target_skeleton_id = 0;
			}
		} break;
	}
}


// =============================================================================
//  Target skeleton
// =============================================================================

void SkeletonAnimator::set_target_skeleton_path(const NodePath &p_path) {
	_target_skeleton_path = p_path;
	_target_skeleton_id   = 0;

	if (!is_inside_tree() || p_path.is_empty())
		return;

	Node *node = get_node_or_null(p_path);
	if (!node) {
		WARN_PRINT("SkeletonAnimator: target skeleton path '" + String(p_path) + "' not found.");
		return;
	}
	Skeleton *sk = Object::cast_to<Skeleton>(node);
	if (!sk) {
		WARN_PRINT("SkeletonAnimator: node at path '" + String(p_path) + "' is not a Skeleton.");
		return;
	}

	_target_skeleton_id = sk->get_instance_id();
	_rebuild_bone_map();
}

NodePath SkeletonAnimator::get_target_skeleton_path() const {
	return _target_skeleton_path;
}


// =============================================================================
//  Animation resource
// =============================================================================

void SkeletonAnimator::set_animation(Ref<Animation> p_animation, bool p_duplicate) {
	ERR_FAIL_COND(!p_animation.is_valid());
    if (p_duplicate) {
        _animation = p_animation->duplicate(false);
    } else {
        _animation = p_animation;
    }
	_initialized = false;
	_position    = 0.0f;
	_loop_count  = 0;
}

Ref<Animation> SkeletonAnimator::get_animation() const {
	return _animation;
}


// =============================================================================
//  Property accessors
// =============================================================================

void SkeletonAnimator::set_apply_mode(AnimationApplyMode p_mode) { _apply_mode = p_mode; }
SkeletonAnimator::AnimationApplyMode SkeletonAnimator::get_apply_mode() const { return _apply_mode; }

void SkeletonAnimator::set_position(float p_position) { _position = p_position; }
float SkeletonAnimator::get_position() const { return _position; }

void SkeletonAnimator::set_multiplier(float p_multiplier) { _multiplier = p_multiplier; }
float SkeletonAnimator::get_multiplier() const { return _multiplier; }

void SkeletonAnimator::set_mirror(bool p_mirror) { _mirror = p_mirror; }
bool SkeletonAnimator::get_mirror() const { return _mirror; }

void SkeletonAnimator::set_fade(bool p_fade) { _fade = p_fade; }
bool SkeletonAnimator::get_fade() const { return _fade; }

void SkeletonAnimator::set_fade_ratio(float p_ratio) {
	_fade_ratio = CLAMP(p_ratio, 0.0f, 1.0f);
}
float SkeletonAnimator::get_fade_ratio() const { return _fade_ratio; }

void SkeletonAnimator::set_playing_speed(float p_speed) {
	_speed = p_speed;
	if (_animation.is_valid()) {
		emit_signal("animation_change", get_animation_name(), _loop_count, _position, _speed);
	}
}
float SkeletonAnimator::get_playing_speed() const { return _speed; }

void SkeletonAnimator::set_loop_count(int p_loop) { _loop_count = p_loop; }
int SkeletonAnimator::get_loop_count() const { return _loop_count; }

void SkeletonAnimator::set_bone_filter(PoolStringArray p_bones) { _bone_filter = p_bones; }
PoolStringArray SkeletonAnimator::get_bone_filter() const { return _bone_filter; }

void SkeletonAnimator::set_ctrl_bone_name(const String &p_name) { _ctrl_bone_name = p_name; }
String SkeletonAnimator::get_ctrl_bone_name() const { return _ctrl_bone_name; }

void SkeletonAnimator::set_ctrl_bone_fps(float p_fps) {
	ERR_FAIL_COND(p_fps <= 0.0f);
	_ctrl_bone_fps = p_fps;
}
float SkeletonAnimator::get_ctrl_bone_fps() const { return _ctrl_bone_fps; }

void SkeletonAnimator::set_ctrl_bone_tolerance(float p_tolerance) {
	ERR_FAIL_COND(p_tolerance <= 0.0f);
	_ctrl_bone_tolerance = p_tolerance;
}
float SkeletonAnimator::get_ctrl_bone_tolerance() const { return _ctrl_bone_tolerance; }

void SkeletonAnimator::set_auto_process(bool p_enable) {
	_auto_process = p_enable;
	set_process(p_enable);
}
bool SkeletonAnimator::get_auto_process() const { return _auto_process; }


// =============================================================================
//  State queries
// =============================================================================

SkeletonAnimator::AnimationState SkeletonAnimator::get_state() const { return _state; }

bool SkeletonAnimator::is_playing() const {
	if (!_animation.is_valid()) return false;
	return (_state != ANIMATION_STATE_NONE && _state != ANIMATION_STATE_EXITED);
}

bool SkeletonAnimator::is_middle() const {
	if (!_animation.is_valid()) return false;
	if (_loop_count > 0)
		return (_state == ANIMATION_STATE_LOOPING);
	return is_playing();
}

bool SkeletonAnimator::is_looping() const {
	if (!_animation.is_valid()) return false;
	return (_position > _loop_start && _position < _loop_end);
}

bool SkeletonAnimator::is_exiting() const {
	if (!_animation.is_valid()) return false;
	return (_state == ANIMATION_STATE_EXITING);
}

bool SkeletonAnimator::has_ended() const {
	if (!_animation.is_valid()) return false;
	return (_state == ANIMATION_STATE_EXITED && _position >= _animation->get_length());
}

bool SkeletonAnimator::is_using_bone(const String &p_bone) const {
	return _bone_uses.has(p_bone);
}

int SkeletonAnimator::get_bone_uses(const String &p_bone) const {
	return _bone_uses.get(p_bone, 0);
}

float SkeletonAnimator::get_loop_start() const { return _loop_start; }
float SkeletonAnimator::get_loop_end()   const { return _loop_end;   }

float SkeletonAnimator::get_animation_looped_length() const {
	if (!_animation.is_valid() || !_initialized)
		return 0.0f;
	// -1 loop_count means infinite — return -1 to signal that to callers.
	if (_loop_count == -1)
		return -1.0f;
	float loop_segment = _loop_end - _loop_start;
	return (_animation->get_length() - loop_segment) + loop_segment * (_loop_count + 1);
}

String SkeletonAnimator::get_animation_name() const {
	if (!_animation.is_valid()) return "";
	return _animation->get_name();
}


// =============================================================================
//  Playback
// =============================================================================

void SkeletonAnimator::play(float p_time, int p_loop, float p_speed) {
	ERR_FAIL_COND_MSG(!_animation.is_valid(), "SkeletonAnimator::play called with no animation set.");

	_position        = p_time;
	_speed           = p_speed;
	_state           = ANIMATION_STATE_STARTING;
	_fade_multiplier = 1.0f;
	_fade_position   = 0.0f;
	_loop_count      = p_loop;
	_initialized     = true;
	_pending_stop    = false;
	_playing         = true;

	// Parse loop region from the control bone.
	_parse_ctrl_bone();

	// Build bone_uses map for this animation.
	_bone_uses.clear();
	for (int tk = 0; tk < _animation->get_track_count(); tk++) {
		NodePath path = _animation->track_get_path(tk);
		if (path.get_subname_count() == 1) {
			String bone_name = path.get_subname(0);
			_bone_uses[bone_name] = _animation->track_get_key_count(tk);
		}
	}

	emit_signal("animation_change", get_animation_name(), _loop_count, _position, _speed);
	emit_signal("animation_start", get_animation_name());
}


void SkeletonAnimator::stop(bool p_immediate) {
	if (!_animation.is_valid())
		return;

	_loop_count = 0;

	if (p_immediate) {
		// Flag the stop so process_animation can finish its current frame cleanly
		// before tearing down state. If called outside of process_animation
		// (e.g. from script), execute immediately.
		if (_playing) {
			_pending_stop = true;
		} else {
			_do_stop_immediate();
		}
	} else if (_state != ANIMATION_STATE_EXITING && _state != ANIMATION_STATE_EXITED) {
		emit_signal("animation_change", get_animation_name(), 0, _position, _speed);
		_state = ANIMATION_STATE_EXITING;
	}
}


// =============================================================================
//  process_animation
// =============================================================================

void SkeletonAnimator::process_animation(float p_delta) {
	if (!_animation.is_valid())
		return;

	float delta = p_delta * _speed;

	// --- Fade multiplier update ---
	if (_fade) {
		if (_state == ANIMATION_STATE_STARTING) {
			if (_loop_start < _ctrl_bone_tolerance) {
				// Animation has no startup segment; fade in over fade_ratio time.
				_fade_multiplier = CLAMP(_fade_position / _fade_ratio, 0.0f, 1.0f);
				float fade_target = (_loop_start > 0.0f) ? _loop_start : (_fade_ratio * _animation->get_length());
				if (_fade_position < fade_target) {
					_fade_position += delta;
					delta = 0.0f;
				} else {
					_fade_multiplier = 1.0f;
				}
			}
			if (_position > _loop_start) {
				_fade_multiplier = 1.0f;
				_fade_position   = _fade_ratio * _animation->get_length();
			}
		} else if (_state == ANIMATION_STATE_EXITING) {
			float anim_length = _animation->get_length();
			float tail_length = anim_length - _loop_end;
			if (tail_length < _ctrl_bone_tolerance && (_position + delta) > anim_length) {
				// Very short (or missing) outro segment: use the time-tracked fade.
				float fade_abs = _fade_ratio * anim_length;
				if (_fade_position > fade_abs)
					_fade_position = fade_abs;
				_fade_multiplier *= CLAMP(_fade_position / fade_abs, 0.0f, 1.0f);
				if (_fade_position > 0.0f) {
					_fade_position -= delta;
					delta = 0.0f;
				}
			} else {
				// Outro segment exists: fade over the last fade_ratio fraction of total length.
				float fade_length = anim_length * _fade_ratio;
				_fade_multiplier *= 1.0f - CLAMP((_position - (anim_length - fade_length)) / fade_length, 0.0f, 1.0f);
			}
		}
	}

	// --- State machine ---
	if (_playing) {
		switch (_state) {
			case ANIMATION_STATE_STARTING: {
				if (_position > _loop_start) {
					_state = (_loop_count == 0) ? ANIMATION_STATE_EXITING : ANIMATION_STATE_LOOPING;
					emit_signal("animation_change", get_animation_name(), _loop_count, _position, _speed);
				}
			} break;

            case ANIMATION_STATE_LOOPING: {
                if (_loop_start >= _animation->get_length()) {
                    _state        = ANIMATION_STATE_EXITING;
                    _pending_stop = true;
                    break;
                }
                float fps = _ctrl_bone_fps;
                if (roundf((_position + delta) * fps) >= roundf(_loop_end * fps)) {
                    bool has_outro = (_loop_end < _animation->get_length() - _ctrl_bone_tolerance);

                    if (_loop_count > 0)
                        _loop_count--;

                    if (_loop_count == 0) {
                        if (!has_outro) {
                            _position = _animation->get_length();
                        }
                        _state = ANIMATION_STATE_EXITING;
                        emit_signal("animation_change", get_animation_name(), _loop_count, _position, _speed);
                    } else {
                        // _loop_count > 0 or infinite (-1): jump back and keep looping.
                        _position = _loop_start;
                    }
                }
            } break;

			case ANIMATION_STATE_EXITING: {
				if ((_position + delta) > _animation->get_length()) {
					_state        = ANIMATION_STATE_EXITED;
					_pending_stop = true;
				}
			} break;

			default:
				break;
		}

		// Apply the pose for this frame, then advance position.
		if (_animation.is_valid()) {
			bool infinite   = (_loop_count == -1);
			bool in_range   = (_position < _animation->get_length());
			if (infinite || in_range) {
				apply_animation(_apply_mode, _animation, _position, _multiplier * _fade_multiplier, _bone_filter, _mirror);
				_position += delta;
			}
		}
	}

	// Deferred stop: execute after the pose has been applied for this frame.
	if (_pending_stop) {
		_do_stop_immediate();
	}
}


// =============================================================================
//  apply_animation
// =============================================================================

void SkeletonAnimator::apply_animation(
		AnimationApplyMode p_mode,
		Ref<Animation> p_animation,
		float p_position,
		float p_multiplier,
		PoolStringArray p_bone_filter,
		bool p_mirror)
{
	if (!p_animation.is_valid())
		return;

	Skeleton *sk = _get_skeleton();
	if (!sk)
		return;

	for (int tk = 0; tk < p_animation->get_track_count(); tk++) {
		if (p_animation->track_get_type(tk) != Animation::TYPE_TRANSFORM)
			continue;

		Vector3 t_pos;
		Quat    t_rot;
		Vector3 t_scale;
		if (p_animation->transform_track_interpolate(tk, p_position, &t_pos, &t_rot, &t_scale) != OK)
			continue;

		NodePath track_path = p_animation->track_get_path(tk);
		if (track_path.get_subname_count() != 1)
			continue;

		String bone = track_path.get_subname(0);

		// Skip the control bone — it carries metadata, not pose data.
		if (bone == _ctrl_bone_name)
			continue;

		// Bone filter: inclusion prefixes with optional !L / !R exclusions.
		if (!p_bone_filter.empty()) {
			bool include = false;
			for (int i = 0; i < p_bone_filter.size(); i++) {
				String filter = p_bone_filter.get(i);
				if (filter.begins_with("!"))
					continue; // these are exclusion tokens, handled below
				if (bone.begins_with(filter)) {
					if (p_bone_filter.has("!L") && bone.ends_with("_L"))
						continue;
					if (p_bone_filter.has("!R") && bone.ends_with("_R"))
						continue;
					include = true;
					break;
				}
			}
			if (!include)
				continue;
		}

		// Mirror: swap _L / _R and flip rotation around the YZ plane.
		if (p_mirror) {
			if (bone.ends_with("_L"))
				bone = bone.substr(0, bone.length() - 1) + "R";
			else if (bone.ends_with("_R"))
				bone = bone.substr(0, bone.length() - 1) + "L";
			t_rot.x *= -1.0f;
			t_rot.w *= -1.0f;
		}

		// Bone index lookup: try cached map first, then linear search + cache.
		int bone_idx = _bone_id_mapping.get(bone, -1);
		if (bone_idx == -1) {
			bone_idx = sk->find_bone(bone);
			if (bone_idx != -1) {
				// Cache so we don't linear-scan again next frame.
				_bone_id_mapping[bone] = bone_idx;
			}
		}
		if (bone_idx == -1)
			continue;

		Transform current = sk->get_bone_pose(bone_idx);

		switch (p_mode) {
			case ANIMATION_APPLY_MODE_ADDITIVE: {
				Transform target = current.translated(t_pos);
				target.basis = Basis(target.basis.orthonormalized().get_rotation_quat() * t_rot)
								.scaled(current.basis.get_scale())
								.scaled(t_scale);
				current = current.interpolate_with(target, p_multiplier);
			} break;

			case ANIMATION_APPLY_MODE_OVERRIDE: {
				Transform target = Transform(Basis(t_rot).scaled(t_scale), t_pos);
				current = current.interpolate_with(target, p_multiplier);
			} break;

			case ANIMATION_APPLY_MODE_OVERRIDE_ROTATION: {
				Transform target = current;
				target.basis = Basis(t_rot).scaled(current.basis.get_scale());
				current = current.interpolate_with(target, p_multiplier);
			} break;
		}

		sk->set_bone_pose(bone_idx, current);
	}
}


// =============================================================================
//  Save / load state
// =============================================================================

Dictionary SkeletonAnimator::save_state() const {
	Dictionary d;
	d["animation"]    = _animation;
	d["state"]        = (int)_state;
	d["playing"]      = _playing;
	d["position"]     = _position;
	d["speed"]        = _speed;
	d["loop_count"]   = _loop_count;
	d["loop_start"]   = _loop_start;
	d["loop_end"]     = _loop_end;
	d["fade_mult"]    = _fade_multiplier;
	d["fade_pos"]     = _fade_position;
	d["initialized"]  = _initialized;
	return d;
}

void SkeletonAnimator::load_state(Dictionary p_state) {
	_animation       = p_state.get("animation",   Ref<Animation>());
	_state           = (AnimationState)(int)p_state.get("state",       (int)ANIMATION_STATE_NONE);
	_playing         = p_state.get("playing",     false);
	_position        = p_state.get("position",    0.0f);
	_speed           = p_state.get("speed",       1.0f);
	_loop_count      = p_state.get("loop_count",  0);
	_loop_start      = p_state.get("loop_start",  0.0f);
	_loop_end        = p_state.get("loop_end",    0.0f);
	_fade_multiplier = p_state.get("fade_mult",   1.0f);
	_fade_position   = p_state.get("fade_pos",    0.0f);
	_initialized     = p_state.get("initialized", false);
}


// =============================================================================
//  Bind methods
// =============================================================================

void SkeletonAnimator::_bind_methods() {

	// Target skeleton
	ClassDB::bind_method(D_METHOD("set_target_skeleton_path", "path"),  &SkeletonAnimator::set_target_skeleton_path);
	ClassDB::bind_method(D_METHOD("get_target_skeleton_path"),          &SkeletonAnimator::get_target_skeleton_path);

	// Animation resource
	ClassDB::bind_method(D_METHOD("set_animation", "animation", "duplicate"), &SkeletonAnimator::set_animation, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_animation"),                            &SkeletonAnimator::get_animation);

	// Apply mode
	ClassDB::bind_method(D_METHOD("set_apply_mode", "mode"), &SkeletonAnimator::set_apply_mode);
	ClassDB::bind_method(D_METHOD("get_apply_mode"),         &SkeletonAnimator::get_apply_mode);

	// Position / multiplier
	ClassDB::bind_method(D_METHOD("set_position",   "position"),   &SkeletonAnimator::set_position);
	ClassDB::bind_method(D_METHOD("get_position"),                 &SkeletonAnimator::get_position);
	ClassDB::bind_method(D_METHOD("set_multiplier", "multiplier"), &SkeletonAnimator::set_multiplier);
	ClassDB::bind_method(D_METHOD("get_multiplier"),               &SkeletonAnimator::get_multiplier);

	// Mirror
	ClassDB::bind_method(D_METHOD("set_mirror", "mirror"), &SkeletonAnimator::set_mirror);
	ClassDB::bind_method(D_METHOD("get_mirror"),           &SkeletonAnimator::get_mirror);

	// Fade
	ClassDB::bind_method(D_METHOD("set_fade",       "fade"),  &SkeletonAnimator::set_fade);
	ClassDB::bind_method(D_METHOD("get_fade"),                &SkeletonAnimator::get_fade);
	ClassDB::bind_method(D_METHOD("set_fade_ratio", "ratio"), &SkeletonAnimator::set_fade_ratio);
	ClassDB::bind_method(D_METHOD("get_fade_ratio"),          &SkeletonAnimator::get_fade_ratio);

	// Speed
	ClassDB::bind_method(D_METHOD("set_playing_speed", "speed"), &SkeletonAnimator::set_playing_speed);
	ClassDB::bind_method(D_METHOD("get_playing_speed"),          &SkeletonAnimator::get_playing_speed);

	// Loop
	ClassDB::bind_method(D_METHOD("set_loop_count", "count"), &SkeletonAnimator::set_loop_count);
	ClassDB::bind_method(D_METHOD("get_loop_count"),          &SkeletonAnimator::get_loop_count);

	// Bone filter
	ClassDB::bind_method(D_METHOD("set_bone_filter", "bones"), &SkeletonAnimator::set_bone_filter);
	ClassDB::bind_method(D_METHOD("get_bone_filter"),          &SkeletonAnimator::get_bone_filter);

	// Ctrl bone config
	ClassDB::bind_method(D_METHOD("set_ctrl_bone_name",      "name"),      &SkeletonAnimator::set_ctrl_bone_name);
	ClassDB::bind_method(D_METHOD("get_ctrl_bone_name"),                   &SkeletonAnimator::get_ctrl_bone_name);
	ClassDB::bind_method(D_METHOD("set_ctrl_bone_fps",       "fps"),       &SkeletonAnimator::set_ctrl_bone_fps);
	ClassDB::bind_method(D_METHOD("get_ctrl_bone_fps"),                    &SkeletonAnimator::get_ctrl_bone_fps);
	ClassDB::bind_method(D_METHOD("set_ctrl_bone_tolerance", "tolerance"), &SkeletonAnimator::set_ctrl_bone_tolerance);
	ClassDB::bind_method(D_METHOD("get_ctrl_bone_tolerance"),              &SkeletonAnimator::get_ctrl_bone_tolerance);

	// Auto process
	ClassDB::bind_method(D_METHOD("set_auto_process", "enable"), &SkeletonAnimator::set_auto_process);
	ClassDB::bind_method(D_METHOD("get_auto_process"),           &SkeletonAnimator::get_auto_process);

	// State queries
	ClassDB::bind_method(D_METHOD("get_state"),     &SkeletonAnimator::get_state);
	ClassDB::bind_method(D_METHOD("is_playing"),    &SkeletonAnimator::is_playing);
	ClassDB::bind_method(D_METHOD("is_middle"),     &SkeletonAnimator::is_middle);
	ClassDB::bind_method(D_METHOD("is_looping"),    &SkeletonAnimator::is_looping);
	ClassDB::bind_method(D_METHOD("has_ended"),     &SkeletonAnimator::has_ended);
	ClassDB::bind_method(D_METHOD("is_exiting"),    &SkeletonAnimator::is_exiting);
	ClassDB::bind_method(D_METHOD("get_loop_start"), &SkeletonAnimator::get_loop_start);
	ClassDB::bind_method(D_METHOD("get_loop_end"),   &SkeletonAnimator::get_loop_end);

	ClassDB::bind_method(D_METHOD("is_using_bone", "bone"),  &SkeletonAnimator::is_using_bone);
	ClassDB::bind_method(D_METHOD("get_bone_uses", "bone"),  &SkeletonAnimator::get_bone_uses);

	ClassDB::bind_method(D_METHOD("get_animation_looped_length"), &SkeletonAnimator::get_animation_looped_length);
	ClassDB::bind_method(D_METHOD("get_animation_name"),          &SkeletonAnimator::get_animation_name);

	// Playback
	ClassDB::bind_method(D_METHOD("play",  "time", "loop", "speed"), &SkeletonAnimator::play, DEFVAL(0.0f), DEFVAL(1), DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("stop",  "immediate"),             &SkeletonAnimator::stop);
	ClassDB::bind_method(D_METHOD("process_animation", "delta"),     &SkeletonAnimator::process_animation);
	ClassDB::bind_method(D_METHOD("apply_animation",
			"mode", "animation", "position", "multiplier", "bone_filter", "mirror"),
			&SkeletonAnimator::apply_animation, DEFVAL(false));

	// Save / load state
	ClassDB::bind_method(D_METHOD("save_state"),          &SkeletonAnimator::save_state);
	ClassDB::bind_method(D_METHOD("load_state", "state"), &SkeletonAnimator::load_state);

	// Enum constants
	BIND_ENUM_CONSTANT(ANIMATION_STATE_NONE);
	BIND_ENUM_CONSTANT(ANIMATION_STATE_STARTING);
	BIND_ENUM_CONSTANT(ANIMATION_STATE_LOOPING);
	BIND_ENUM_CONSTANT(ANIMATION_STATE_EXITING);
	BIND_ENUM_CONSTANT(ANIMATION_STATE_EXITED);

	BIND_ENUM_CONSTANT(ANIMATION_APPLY_MODE_ADDITIVE);
	BIND_ENUM_CONSTANT(ANIMATION_APPLY_MODE_OVERRIDE);
	BIND_ENUM_CONSTANT(ANIMATION_APPLY_MODE_OVERRIDE_ROTATION);

	// Signals (with named parameters for GDScript autocomplete)
	ADD_SIGNAL(MethodInfo("animation_change",
		PropertyInfo(Variant::STRING,  "name"),
		PropertyInfo(Variant::INT,     "loop_count"),
		PropertyInfo(Variant::REAL,    "position"),
		PropertyInfo(Variant::REAL,    "speed")));
	ADD_SIGNAL(MethodInfo("animation_start",
		PropertyInfo(Variant::STRING, "name")));
	ADD_SIGNAL(MethodInfo("animation_end",
		PropertyInfo(Variant::STRING, "name")));
}


// =============================================================================
//  Constructor / Destructor
// =============================================================================

SkeletonAnimator::SkeletonAnimator() {
}

SkeletonAnimator::~SkeletonAnimator() {
}
