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

/*! \file
  \brief Interface for class TriangleMeshViewerDisplay.
*/

#ifndef _triangle_mesh_viewer_display_h_
#define _triangle_mesh_viewer_display_h_

#include <deque>

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>

#include "common.h"
#include "parameters_render.h"
#include "random.h"
#include "triangle_mesh.h"

class TriangleMeshViewer;

//! Per-mesh GPU resources (VBO + IBO + VAO).
struct MeshGPU
{
  QOpenGLVertexArrayObject vao;
  QOpenGLBuffer            vbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer            ibo{QOpenGLBuffer::IndexBuffer};
  uint  triangles_colour0{0};  ///< triangles rendered with colour set 0
  uint  triangles_total  {0};  ///< total triangle count
  float emissive         {0};  ///< mesh emissive factor (0 = none)
};

//! Contains the actual rendering functionality of a TriangleMeshViewer.
class TriangleMeshViewerDisplay : public QOpenGLWidget, protected QOpenGLFunctions
{
 private:

  Q_OBJECT;

 public:

  //! Constructor.
  TriangleMeshViewerDisplay(TriangleMeshViewer* parent,
                            const ParametersRender* param,
                            const std::vector<const TriangleMesh*>& m,
                            bool verbose);

  //! Destructor
  ~TriangleMeshViewerDisplay();

  //! Specify a minimum size
  QSize minimumSizeHint() const;

  //! Guideline size
  QSize sizeHint() const;

  //! Set the mesh being rendered (rebuilds GPU buffers).
  void set_mesh(const std::vector<const TriangleMesh*>& m);

 protected:

  //! Called to repaint GL area.
  void paintGL() override;

  //! Set up OpenGL (compile shaders, create initial state).
  void initializeGL() override;

  //! Deal with resize.
  void resizeGL(int w, int h) override;

 public slots:

  //! Called to redisplay scene
  void draw_frame(const XYZ& p, const XYZ& l, const XYZ& u, float r, float t);

 private:

  //! Need to know this to update framerate text
  TriangleMeshViewer& _notify;

  //! Control logging
  const bool _verbose;

  //! The meshes being displayed (NOT owned here).
  std::vector<const TriangleMesh*> mesh;

  //! Per-mesh GPU resources (owned here).
  std::vector<std::unique_ptr<MeshGPU>> mesh_gpu;

  //! Pointer to the rendering parameters.
  const ParametersRender* parameters;

  //! GLSL shader program.
  QOpenGLShaderProgram shader;

  //! Frame count.
  uint frame_number;

  //! Display area width/height (in pixels).
  uint _width;
  uint _height;

  //! Time frames for FPS measurement.
  QElapsedTimer frame_time;

  //! Time since FPS last reported.
  QElapsedTimer frame_time_reported;

  //! Queue of frame times to average.
  std::deque<uint> frame_times;

  //@{
  //! Parameter of camera position.
  XYZ camera_position;
  XYZ camera_lookat;
  XYZ camera_up;
  //@}

  //@{
  //! Parameters of object
  float object_tilt;
  float object_rotation;
  //@}

  //! Compute background colour from render parameters and camera height.
  FloatRGBA background_colour() const;

  //! Build/rebuild GPU buffers for all current meshes.
  void build_gpu_buffers();

  //! Release all GPU buffers.
  void destroy_gpu_buffers();

  //! Log any pending GL errors.
  void check_for_gl_errors(const char*);
};

#endif
