# Camera Control

## 概述

Camera Control 示例展示各种相机控制模式，包括轨道相机、第一人称、路径动画等。本示例涵盖:

- **轨道相机**: 围绕目标旋转查看
- **第一人称**: WASD + 鼠标控制
- **相机路径**: 平滑动画过渡
- **触摸手势**: 移动平台支持
- **约束系统**: FOV/距离/角度限制

## 轨道相机实现

```cpp
// orbit_camera.cpp
#include <filament/Camera.h>
#include <math/vec3.h>
#include <math/mat4.h>

using namespace filament;
using namespace math;

class OrbitCamera {
public:
    OrbitCamera(float3 target, float distance)
        : m_target(target), m_distance(distance) {
        updatePosition();
    }

    void rotate(float deltaAzimuth, float deltaElevation) {
        m_azimuth += deltaAzimuth;
        m_elevation = clamp(m_elevation + deltaElevation, -89.0f, 89.0f);
        updatePosition();
    }

    void zoom(float delta) {
        m_distance = clamp(m_distance * (1.0f - delta * 0.1f), 1.0f, 100.0f);
        updatePosition();
    }

    void pan(float deltaX, float deltaY) {
        float3 right = normalize(cross(m_up, getDirection()));
        float3 up = m_up;
        m_target += right * deltaX + up * deltaY;
        updatePosition();
    }

    mat4f getViewMatrix() const {
        return mat4f::lookAt(m_position, m_target, m_up);
    }

private:
    void updatePosition() {
        float rad_azi = radians(m_azimuth);
        float rad_ele = radians(m_elevation);

        m_position = m_target + float3{
            m_distance * cos(rad_ele) * sin(rad_azi),
            m_distance * sin(rad_ele),
            m_distance * cos(rad_ele) * cos(rad_azi)
        };
    }

    float3 getDirection() const {
        return normalize(m_target - m_position);
    }

    float3 m_position;
    float3 m_target;
    float3 m_up{0, 1, 0};
    float m_distance;
    float m_azimuth = 0.0f;
    float m_elevation = 30.0f;
};
```

## 第一人称相机

```cpp
// fps_camera.cpp
class FPSCamera {
public:
    void update(float deltaTime) {
        float3 movement{0};
        if (m_forward) movement += m_front;
        if (m_backward) movement -= m_front;
        if (m_left) movement -= m_right;
        if (m_right) movement += m_right;

        if (length(movement) > 0) {
            m_position += normalize(movement) * m_speed * deltaTime;
        }
    }

    void look(float deltaX, float deltaY) {
        m_yaw += deltaX * m_sensitivity;
        m_pitch = clamp(m_pitch + deltaY * m_sensitivity, -89.0f, 89.0f);
        updateVectors();
    }

    mat4f getViewMatrix() const {
        return mat4f::lookAt(m_position, m_position + m_front, m_up);
    }

    void handleKey(int key, bool pressed) {
        if (key == 'W') m_forward = pressed;
        else if (key == 'S') m_backward = pressed;
        else if (key == 'A') m_left = pressed;
        else if (key == 'D') m_right = pressed;
    }

private:
    void updateVectors() {
        m_front = normalize(float3{
            cos(radians(m_pitch)) * cos(radians(m_yaw)),
            sin(radians(m_pitch)),
            cos(radians(m_pitch)) * sin(radians(m_yaw))
        });
        m_right = normalize(cross(m_front, {0, 1, 0}));
        m_up = normalize(cross(m_right, m_front));
    }

    float3 m_position{0, 1.6f, 0};
    float3 m_front{0, 0, -1};
    float3 m_up, m_right;
    float m_yaw = -90.0f;
    float m_pitch = 0.0f;
    float m_speed = 5.0f;
    float m_sensitivity = 0.1f;
    bool m_forward = false, m_backward = false;
    bool m_left = false, m_right = false;
};
```

## 相机路径动画

```cpp
// camera_path.cpp
struct Keyframe {
    float time;
    float3 position;
    quatf rotation;
};

class CameraPath {
public:
    void addKeyframe(float t, float3 pos, quatf rot) {
        m_keyframes.push_back({t, pos, rot});
    }

    bool evaluate(float time, float3& pos, quatf& rot) {
        if (m_keyframes.size() < 2) return false;

        // Find keyframes
        for (size_t i = 0; i < m_keyframes.size() - 1; ++i) {
            if (time >= m_keyframes[i].time &&
                time <= m_keyframes[i+1].time) {

                float t = (time - m_keyframes[i].time) /
                         (m_keyframes[i+1].time - m_keyframes[i].time);

                // Smooth interpolation
                t = t * t * (3.0f - 2.0f * t);

                pos = mix(m_keyframes[i].position,
                         m_keyframes[i+1].position, t);
                rot = slerp(m_keyframes[i].rotation,
                           m_keyframes[i+1].rotation, t);
                return true;
            }
        }
        return false;
    }

private:
    std::vector<Keyframe> m_keyframes;
};
```

## Android 触摸控制

```kotlin
// CameraGestures.kt
class CameraGestures(private val camera: OrbitCamera) {
    private val gestureDetector = GestureDetector(context,
        object : SimpleOnGestureListener() {
            override fun onScroll(
                e1: MotionEvent,
                e2: MotionEvent,
                distanceX: Float,
                distanceY: Float
            ): Boolean {
                camera.rotate(distanceX * 0.5f, distanceY * 0.5f)
                return true
            }
        })

    private val scaleDetector = ScaleGestureDetector(context,
        object : SimpleOnScaleGestureListener() {
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                camera.zoom(1.0f - detector.scaleFactor)
                return true
            }
        })

    fun onTouchEvent(event: MotionEvent) {
        gestureDetector.onTouchEvent(event)
        scaleDetector.onTouchEvent(event)
    }
}
```

## 常见问题

### Q1: 相机抖动

使用平滑插值:

```cpp
class SmoothedCamera {
    float3 m_current, m_target;

    void update(float dt) {
        m_current = mix(m_current, m_target,
                       1.0f - exp(-5.0f * dt));
    }
};
```

### Q2: 万向锁

使用四元数:

```cpp
quatf rotation = quatf::fromAxisAngle({0, 1, 0}, angle);
```

## 相关文档

- [./04-gltf-viewer.md](./04-gltf-viewer.md) - glTF查看器
- [./06-animation-player.md](./06-animation-player.md) - 动画播放器
