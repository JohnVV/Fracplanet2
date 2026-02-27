/**************************************************************************/
/*  Copyright 2009 Tim Day                                                */
/*                                                                        */
/*  This file is part of Fracplanet                                       */
/*                                                                        */
/*  Fracplanet is free software: you can redistribute it and/or modify    */
/*  it under the terms of the GNU General Public License as published by  */
/*  the Free Software Foundation, either version 3 of the License, or     */
/*  (at your option) any later version.                                   */
/*                                                                        */
/*  Fracplanet is distributed in the hope that it will be useful,         */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/*  GNU General Public License for more details.                          */
/*                                                                        */
/*  You should have received a copy of the GNU General Public License     */
/*  along with Fracplanet.  If not, see <http://www.gnu.org/licenses/>.   */
/**************************************************************************/

#include "triangle_mesh_viewer_display.h"
#include "triangle_mesh_viewer.h"
#include "matrix33.h"

#include <QMatrix4x4>
#include <QVector3D>

// ---------------------------------------------------------------------------
// GLSL 1.50 shaders (OpenGL 3.2 core)
// ---------------------------------------------------------------------------

static const char* VERT_SRC = R"glsl(
#version 150 core

// Object-space transform (tilt + spin)
uniform mat4  u_model;
// Camera (view) transform
uniform mat4  u_view;
// Projection transform
uniform mat4  u_proj;
// Normal matrix = mat3(transpose(inverse(u_model)))
// For the pure-rotation object transforms used here this equals mat3(u_model).
uniform mat3  u_normal_mat;

// Lighting (all in world space)
uniform vec3  u_light_dir;   // unit vector toward the light source
uniform float u_ambient;     // ambient intensity [0,1]

// Per-draw
uniform float u_emissive;    // mesh emissive factor (0 = non-emissive terrain)
uniform int   u_colour_set;  // 0 or 1 — which colour attribute to use

// Per-vertex
in vec3 a_position;
in vec3 a_normal;
in vec4 a_colour0;    // GL_UNSIGNED_BYTE, normalized to [0,1]
in vec4 a_colour1;

out vec4 v_colour;

void main()
{
    // Transform position through model then view then projection
    vec4 world_pos = u_model * vec4(a_position, 1.0);
    gl_Position    = u_proj * u_view * world_pos;

    // Transform normal to world space (object has uniform scale, so no
    // need for full inverse-transpose beyond rotating).
    vec3 n = normalize(u_normal_mat * a_normal);

    // Diffuse Lambertian term
    float ndotl = max(dot(n, u_light_dir), 0.0);

    // Choose colour set
    vec4 raw = (u_colour_set == 0) ? a_colour0 : a_colour1;
    vec3 col = raw.rgb;
    float alpha = raw.a;

    vec3 lit;
    float out_alpha;

    if (u_emissive > 0.0 && alpha < (0.5 / 255.0)) {
        // alpha == 0 (byte) signals an emissive vertex.
        // Emissive and transparent are mutually exclusive in this codebase.
        float ad = 1.0 - u_emissive;
        float em = u_emissive;
        lit       = col * (u_ambient * ad + ndotl * (1.0 - u_ambient) * ad + em);
        out_alpha = 1.0;
    } else {
        // Standard diffuse-only lighting
        lit       = col * (u_ambient + ndotl * (1.0 - u_ambient));
        out_alpha = alpha;
    }

    v_colour = vec4(lit, out_alpha);
}
)glsl";

static const char* FRAG_SRC = R"glsl(
#version 150 core

in  vec4 v_colour;
out vec4 frag_colour;

void main()
{
    frag_colour = v_colour;
}
)glsl";

// ---------------------------------------------------------------------------

TriangleMeshViewerDisplay::TriangleMeshViewerDisplay(
    TriangleMeshViewer* parent,
    const ParametersRender* param,
    const std::vector<const TriangleMesh*>& m,
    bool verbose)
  : QOpenGLWidget(parent)
  , _notify(*parent)
  , _verbose(verbose)
  , mesh(m)
  , parameters(param)
  , frame_number(0)
  , _width(0)
  , _height(0)
  , camera_position(3.0f, 0.0f, 0.0f)
  , camera_lookat(0.0f, 0.0f, 0.0f)
  , camera_up(0.0f, 0.0f, 1.0f)
  , object_tilt(30.0f * static_cast<float>(M_PI) / 180.0f)
  , object_rotation(0.0f)
{
  setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
  frame_time.start();
  frame_time_reported.start();
}

TriangleMeshViewerDisplay::~TriangleMeshViewerDisplay()
{
  makeCurrent();
  destroy_gpu_buffers();
  doneCurrent();
}

QSize TriangleMeshViewerDisplay::minimumSizeHint() const { return QSize(64, 64); }
QSize TriangleMeshViewerDisplay::sizeHint()        const { return QSize(512, 512); }

// ---------------------------------------------------------------------------
// GPU buffer management
// ---------------------------------------------------------------------------

void TriangleMeshViewerDisplay::destroy_gpu_buffers()
{
  mesh_gpu.clear();
}

void TriangleMeshViewerDisplay::build_gpu_buffers()
{
  destroy_gpu_buffers();

  for (uint m = 0; m < mesh.size(); ++m)
    {
      const TriangleMesh* it = mesh[m];
      if (!it || it->vertices() == 0 || it->triangles() == 0) {
        mesh_gpu.push_back(nullptr);
        continue;
      }

      auto gpu = std::make_unique<MeshGPU>();
      gpu->triangles_colour0 = it->triangles_of_colour0();
      gpu->triangles_total   = it->triangles();
      gpu->emissive          = it->emissive();

      gpu->vao.create();
      gpu->vao.bind();

      // --- Vertex buffer ---
      // The Vertex struct is exactly 32 bytes:
      //   offset  0: float[3] position
      //   offset 12: float[3] normal
      //   offset 24: uint8[4] colour[0]
      //   offset 28: uint8[4] colour[1]
      gpu->vbo.create();
      gpu->vbo.bind();
      gpu->vbo.allocate(&it->vertex(0),
                        static_cast<int>(it->vertices() * sizeof(Vertex)));

      const int stride = static_cast<int>(sizeof(Vertex));

      // a_position  (location 0)
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<void*>(0));
      // a_normal    (location 1)
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<void*>(12));
      // a_colour0   (location 2)  — normalized unsigned bytes
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                            reinterpret_cast<void*>(24));
      // a_colour1   (location 3)
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                            reinterpret_cast<void*>(28));

      // --- Index buffer ---
      // Triangle._vertex[3] is a uint[3] array — safe to upload directly.
      gpu->ibo.create();
      gpu->ibo.bind();
      gpu->ibo.allocate(&it->triangle(0).vertex(0),
                        static_cast<int>(it->triangles() * 3 * sizeof(uint)));

      gpu->vao.release();
      gpu->vbo.release();
      gpu->ibo.release();

      if (_verbose)
        std::cerr << "Built GPU buffers for mesh " << m
                  << ": " << it->vertices() << " verts, "
                  << it->triangles() << " tris\n";

      mesh_gpu.push_back(std::move(gpu));
    }
}

void TriangleMeshViewerDisplay::set_mesh(const std::vector<const TriangleMesh*>& m)
{
  mesh = m;
  if (isValid()) {
    makeCurrent();
    build_gpu_buffers();
    doneCurrent();
  }
  // If GL isn't initialised yet, build_gpu_buffers() is called from initializeGL().
}

// ---------------------------------------------------------------------------
// Background colour
// ---------------------------------------------------------------------------

FloatRGBA TriangleMeshViewerDisplay::background_colour() const
{
  if (mesh.empty()) return FloatRGBA(0.0f, 0.0f, 0.0f, 1.0f);

  const XYZ rel =
    Matrix33RotateAboutZ(-object_rotation) *
    Matrix33RotateAboutX(-object_tilt)     *
    camera_position;

  const float h = mesh[0]->geometry().height(rel);
  if (h <= 0.0f) return parameters->background_colour_low;
  if (h >= 1.0f) return parameters->background_colour_high;
  return parameters->background_colour_low
       + h * (parameters->background_colour_high - parameters->background_colour_low);
}

// ---------------------------------------------------------------------------
// GL error check
// ---------------------------------------------------------------------------

void TriangleMeshViewerDisplay::check_for_gl_errors(const char* where)
{
  GLenum error;
  while ((error = glGetError()) != GL_NO_ERROR) {
    std::ostringstream msg;
    msg << "GL error in " << where << " (frame " << frame_number << "): ";
    switch (error) {
      case GL_INVALID_ENUM:      msg << "GL_INVALID_ENUM";      break;
      case GL_INVALID_VALUE:     msg << "GL_INVALID_VALUE";     break;
      case GL_INVALID_OPERATION: msg << "GL_INVALID_OPERATION"; break;
      case GL_OUT_OF_MEMORY:     msg << "GL_OUT_OF_MEMORY";     break;
      default:                   msg << "0x" << std::hex << error; break;
    }
    std::cerr << msg.str() << std::endl;
  }
}

// ---------------------------------------------------------------------------
// initializeGL
// ---------------------------------------------------------------------------

void TriangleMeshViewerDisplay::initializeGL()
{
  initializeOpenGLFunctions();

  if (_verbose) {
    std::cerr << "OpenGL version : "
              << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << "\n";
    std::cerr << "GLSL version   : "
              << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)) << "\n";
    std::cerr << "Multisampling  : "
              << (format().samples() > 1 ? "ON" : "OFF") << "\n";
  }

  // Compile and link shaders
  if (!shader.addShaderFromSourceCode(QOpenGLShader::Vertex, VERT_SRC))
    std::cerr << "Vertex shader error:\n" << shader.log().toStdString() << "\n";

  if (!shader.addShaderFromSourceCode(QOpenGLShader::Fragment, FRAG_SRC))
    std::cerr << "Fragment shader error:\n" << shader.log().toStdString() << "\n";

  // Bind attribute locations before linking
  shader.bindAttributeLocation("a_position", 0);
  shader.bindAttributeLocation("a_normal",   1);
  shader.bindAttributeLocation("a_colour0",  2);
  shader.bindAttributeLocation("a_colour1",  3);

  if (!shader.link())
    std::cerr << "Shader link error:\n" << shader.log().toStdString() << "\n";

  // Fixed GL state
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);

  // If meshes were set before the GL context existed, build buffers now.
  if (!mesh.empty())
    build_gpu_buffers();

  if (_verbose)
    check_for_gl_errors(__PRETTY_FUNCTION__);
}

// ---------------------------------------------------------------------------
// resizeGL
// ---------------------------------------------------------------------------

void TriangleMeshViewerDisplay::resizeGL(int w, int h)
{
  _width  = static_cast<uint>(w);
  _height = static_cast<uint>(h);

  if (_verbose)
    std::cerr << "QOpenGLWidget resized to " << _width << "x" << _height << "\n";

  glViewport(0, 0, w, h);
}

// ---------------------------------------------------------------------------
// paintGL
// ---------------------------------------------------------------------------

void TriangleMeshViewerDisplay::paintGL()
{
  const FloatRGBA bg = background_colour();
  glClearColor(bg.r, bg.g, bg.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (mesh_gpu.empty()) return;

  // --- Build matrices ---

  // Object transform: tilt (X) then spin (Z)
  QMatrix4x4 model;
  model.rotate(static_cast<float>(180.0 / M_PI) * object_tilt,     1, 0, 0);
  model.rotate(static_cast<float>(180.0 / M_PI) * object_rotation, 0, 0, 1);

  // Camera / view transform
  QMatrix4x4 view;
  view.lookAt(
    QVector3D(camera_position.x, camera_position.y, camera_position.z),
    QVector3D(camera_lookat.x,   camera_lookat.y,   camera_lookat.z),
    QVector3D(camera_up.x,       camera_up.y,       camera_up.z));

  // Perspective projection
  const float view_angle =
    static_cast<float>(minimum(90.0,
      _width > _height ? 45.0 : 45.0 * _height / _width));

  QMatrix4x4 proj;
  proj.perspective(view_angle,
                   static_cast<float>(_width) / static_cast<float>(_height),
                   0.01f, 10.0f);

  // Normal matrix: for pure rotations, inverse-transpose == the rotation itself
  QMatrix3x3 normal_mat = model.normalMatrix();

  // Light direction in world space
  const XYZ ld = parameters->illumination_direction();
  QVector3D light_dir(ld.x, ld.y, ld.z);
  light_dir.normalize();

  // --- Render each mesh ---

  shader.bind();
  shader.setUniformValue("u_model",      model);
  shader.setUniformValue("u_view",       view);
  shader.setUniformValue("u_proj",       proj);
  shader.setUniformValue("u_normal_mat", normal_mat);
  shader.setUniformValue("u_light_dir",  light_dir);
  shader.setUniformValue("u_ambient",    parameters->ambient);

  for (uint m = 0; m < mesh_gpu.size(); ++m)
    {
      MeshGPU* gpu = mesh_gpu[m].get();
      if (!gpu) continue;

      shader.setUniformValue("u_emissive", gpu->emissive);

      // Meshes after the first (clouds) are rendered with both face windings
      // so they look correct from above and below.
      const uint passes = (m == 0) ? 1u : 2u;

      if (gpu->emissive == 0.0f && m > 0) {
        // Non-emissive secondary mesh (unused in current codebase, but handle it)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      } else if (m > 0) {
        // Emissive secondary mesh (clouds): no blending — alpha used for emission
        glDisable(GL_BLEND);
      } else {
        glDisable(GL_BLEND);
      }

      gpu->vao.bind();

      for (uint pass = 0; pass < passes; ++pass)
        {
          glCullFace(passes == 2 && pass == 0 ? GL_FRONT : GL_BACK);

          // Draw colour-0 triangles
          if (gpu->triangles_colour0 > 0) {
            shader.setUniformValue("u_colour_set", 0);
            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(gpu->triangles_colour0 * 3),
                           GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(0));
          }

          // Draw colour-1 triangles (offset into IBO by colour0 triangles)
          const uint colour1_count = gpu->triangles_total - gpu->triangles_colour0;
          if (colour1_count > 0) {
            shader.setUniformValue("u_colour_set", 1);
            glDrawElements(
              GL_TRIANGLES,
              static_cast<GLsizei>(colour1_count * 3),
              GL_UNSIGNED_INT,
              reinterpret_cast<void*>(gpu->triangles_colour0 * 3 * sizeof(uint)));
          }
        }

      gpu->vao.release();
    }

  // Restore cull face default
  glCullFace(GL_BACK);
  glDisable(GL_BLEND);
  shader.release();

  if (_verbose)
    check_for_gl_errors(__PRETTY_FUNCTION__);

  // --- FPS tracking ---

  const qint64 dt = frame_time.restart();
  frame_times.push_back(static_cast<uint>(dt));
  while (frame_times.size() > 30) frame_times.pop_front();

  if (frame_time_reported.elapsed() > 500)
    {
      const float avg = std::accumulate(frame_times.begin(), frame_times.end(), 0u)
                        / static_cast<float>(frame_times.size());
      const float fps = 1000.0f / avg;

      uint n_tris = 0, n_verts = 0;
      for (uint m = 0; m < mesh.size(); ++m) {
        if (mesh[m]) {
          n_tris  += mesh[m]->triangles();
          n_verts += mesh[m]->vertices();
        }
      }

      std::ostringstream report;
      report.setf(std::ios::fixed);
      report.precision(1);
      report << "Triangles: " << n_tris
             << ", Vertices: " << n_verts
             << ", FPS: " << fps << "\n";

      _notify.notify(report.str());
      frame_time_reported.restart();
    }
}

// ---------------------------------------------------------------------------
// draw_frame — called by the animation timer in TriangleMeshViewer
// ---------------------------------------------------------------------------

void TriangleMeshViewerDisplay::draw_frame(const XYZ& p, const XYZ& l, const XYZ& u,
                                           float r, float t)
{
  ++frame_number;
  camera_position = p;
  camera_lookat   = l;
  camera_up       = u;
  object_rotation = r;
  object_tilt     = t;
  update();
}
