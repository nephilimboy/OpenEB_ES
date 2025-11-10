# Copyright (c) Prophesee S.A.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed under the License is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and limitations under the License.

# OpenGL
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED COMPONENTS GLES2)

# GLEW
#find_package(GLEW REQUIRED)

# GLFW
find_package(glfw3 REQUIRED)

MetavisionSDK_add_module(ui
    REQUIRED_METAVISION_SDK_MODULES
        PUBLIC
            core
    EXTRA_REQUIRED_PACKAGE "OpenCV COMPONENTS core"
    EXTRA_REQUIRED_PACKAGE "OpenGL"
    EXTRA_REQUIRED_PACKAGE "GLEW"
    EXTRA_REQUIRED_PACKAGE "glfw3"

    VARIABLE_TO_SET "OpenGL_GL_PREFERENCE GLVND"
)

target_link_libraries(metavision_sdk_ui
    PUBLIC
        opencv_core
        glfw
)

option(USE_OPENGL_ES3 "Use OpenGL ES3 instead of OpenGl Desktop (usefull on embedded devices)" OFF)

if(USE_OPENGL_ES3)
    add_definitions(-D_USE_OPENGL_ES3_)
    if(NOT TARGET OpenGL::GLES3)
        ## Previous to cmake 3.27, OpenGL ES targets were not supported...
        ## Let's search for it
        find_library(GLESv2_lib GLESv2)
        find_file(GLES3_header gl3.h PATH_SUFFIXES GLES3)
        get_filename_component(GLES3_header_dir ${GLES3_header} DIRECTORY)

        if(GLESv2_lib MATCHES "NOTFOUND")
            message(NOTICE "The required OpenGL ES2/3 library (libGLESv2) is not found")
            return()
        endif()

        if(GLES3_header MATCHES "NOTFOUND")
            message(NOTICE "Could not find OpenGL ES3 headers (gl3.h)")
            return()
        endif()

        add_library(GLESv2 UNKNOWN IMPORTED GLOBAL)
        set_target_properties(GLESv2 PROPERTIES 
            IMPORTED_LOCATION ${GLESv2_lib}
            INTERFACE_INCLUDE_DIRECTORIES ${GLES3_header_dir}
        )

        # Provide a meaningful alias to indicate ES3 use, but still use GLESv2 library (as does GLES3)
        add_library(OpenGL::GLES3 ALIAS GLESv2)
    endif()
    
    target_link_libraries(metavision_sdk_ui PUBLIC OpenGL::GLES3)
else()
    target_link_libraries(metavision_sdk_ui 
        PUBLIC 
            OpenGL::GL 
            GLEW::GLEW
    )
endif(USE_OPENGL_ES3)