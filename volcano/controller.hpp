#ifndef _CAMERA_HPP_
#define _CAMERA_HPP_

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class CameraRoam {
public:
    struct Posture {
        float deltatime;
        float lasttime;
        float lastX;
        float lastY;
        float yaw;
        float pitch;
        float enter;
    };
    ~CameraRoam() {}
    CameraRoam() {}
    CameraRoam(float w, float h, glm::vec3 l, glm::vec3 u, glm::vec3 f) {
        loc = l;
        up = u;
        front = f;
        view_mat = glm::lookAt(loc, loc + front, up);
        proj_mat = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);

        posture.deltatime = 0;
        posture.lasttime = 0;
        posture.lastX = w / 2;
        posture.lastY = h / 2;
        posture.yaw = -90.0f;
        posture.pitch = 0.0f;
        posture.enter = false;

        center = glm::vec2(w/2, h/2);
    }

    void onKeyEvent(GLFWwindow* win, int key, int scancode, int action, int mode) {
        float speed = 5 * posture.deltatime;

        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        if (key == GLFW_KEY_S) {
            loc -= speed * front;
        }

        if (key == GLFW_KEY_W) {
            loc += speed * front;
        }

        if (key == GLFW_KEY_A) {
            loc -= glm::normalize(glm::cross(front, up)) * speed;
        }

        if (key == GLFW_KEY_D) {
            loc += glm::normalize(glm::cross(front, up)) * speed;
        }

        loc.y = 0.0;
    }

    void onMouseEvent(GLFWwindow* win, double xpos, double ypos) {
        if (!posture.enter) {
            glfwSetCursorPos(win, center.x, center.y);
            posture.enter = true;
        }

        float xoffset = xpos - posture.lastX;
        float yoffset = ypos - posture.lastY;

        posture.lastX = xpos;
        posture.lastY = ypos;

        float sensitivity = 0.1f;
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        posture.yaw += xoffset;
        posture.pitch += yoffset;

        if (posture.pitch > 89.5f)
            posture.pitch = 89.5f;
        if (posture.pitch < -89.5f)
            posture.pitch = -89.5f;

        front.x = cos(glm::radians(posture.yaw)) * cos(glm::radians(posture.pitch));
        front.y = sin(glm::radians(posture.pitch));
        front.z = sin(glm::radians(posture.yaw)) * cos(glm::radians(posture.pitch));
        front = glm::normalize(front);
    }

    inline void advance() {
        float currenttime = glfwGetTime();
        posture.deltatime = currenttime - posture.lasttime;
        posture.lasttime = currenttime;
    }

    inline void compute() {
        view_mat = glm::lookAt(loc, loc + front, up);
    }

    inline glm::mat4 mvp() const {
        return proj_mat * view_mat;
    }

public:
    struct Posture posture;
    glm::vec3 loc;
    glm::vec3 up;
    glm::vec3 front;
    glm::mat4 view_mat;
    glm::mat4 proj_mat;
    glm::vec2 center;
};

#endif
