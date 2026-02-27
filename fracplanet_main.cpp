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

#include "fracplanet_main.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <boost/program_options/errors.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "image.h"

FracplanetMain::FracplanetMain(QApplication* app,
                               const boost::program_options::variables_map& opts,
                               bool verbose)
  : QMainWindow()
  , _verbose(verbose)
  , application(app)
  , mesh_terrain(nullptr)
  , mesh_cloud(nullptr)
  , parameters_terrain()
  , parameters_cloud()
  , parameters_render(opts)
  , parameters_save(&parameters_render)
  , viewer(nullptr)
  , last_step(0)
  , progress_was_stalled(false)
{
  setWindowTitle("Fracplanet");
  setMinimumSize(1024, 720);
  resize(1280, 800);

  // -----------------------------------------------------------------------
  // Build the splitter — left panel (controls) | right panel (3-D view)
  // -----------------------------------------------------------------------
  QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
  setCentralWidget(splitter);

  // ---- Left panel: scroll area wrapping a tab widget ----
  QScrollArea* scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setMinimumWidth(576);
  scroll->setMaximumWidth(720);

  QTabWidget* tabs = new QTabWidget();
  tabs->setDocumentMode(true);

  control_terrain = new ControlTerrain(this, &parameters_terrain, &parameters_cloud);
  tabs->addTab(control_terrain, "Create");

  control_render = new ControlRender(&parameters_render);
  tabs->addTab(control_render, "Render");

  control_save = new ControlSave(this, &parameters_save);
  tabs->addTab(control_save, "Save");

  control_about = new ControlAbout(application);
  tabs->addTab(control_about, "About");

  scroll->setWidget(tabs);
  splitter->addWidget(scroll);

  // ---- Right panel: the embedded 3-D viewer (created once, never recreated) ----
  viewer.reset(new TriangleMeshViewer(this, &parameters_render,
                                  std::vector<const TriangleMesh*>(), _verbose));
  splitter->addWidget(viewer.get());

  // Give the viewer most of the space: left ~320px, right stretches
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({320, 960});

  // Status bar is part of QMainWindow
  statusBar()->showMessage("Ready — press Regenerate to generate terrain.");
}

FracplanetMain::~FracplanetMain()
{}

// ---------------------------------------------------------------------------
// Progress interface
// ---------------------------------------------------------------------------

void FracplanetMain::progress_start(uint target, const std::string& info)
{
  if (!progress_dialog)
    {
      progress_dialog = std::make_unique<QProgressDialog>("Progress", "Cancel", 0, 100, this);
      progress_dialog->setWindowModality(Qt::WindowModal);
      progress_dialog->setCancelButton(nullptr);
      progress_dialog->setAutoClose(false);
      progress_dialog->setMinimumDuration(0);
    }

  progress_was_stalled = false;
  progress_info        = info;
  progress_dialog->reset();
  progress_dialog->setMaximum(static_cast<int>(target) + 1);
  progress_dialog->setLabelText(progress_info.c_str());
  progress_dialog->show();

  last_step = static_cast<uint>(-1);

  QApplication::setOverrideCursor(Qt::WaitCursor);
  application->processEvents();
}

void FracplanetMain::progress_stall(const std::string& reason)
{
  progress_was_stalled = true;
  progress_dialog->setLabelText(reason.c_str());
  application->processEvents();
}

void FracplanetMain::progress_step(uint step)
{
  if (progress_was_stalled)
    {
      progress_dialog->setLabelText(progress_info.c_str());
      progress_was_stalled = false;
      application->processEvents();
    }

  if (step != last_step)
    {
      progress_dialog->setValue(static_cast<int>(step));
      last_step = step;
      application->processEvents();
    }
}

void FracplanetMain::progress_complete(const std::string& info)
{
  progress_dialog->setLabelText(info.c_str());
  last_step = static_cast<uint>(-1);
  QApplication::restoreOverrideCursor();
  application->processEvents();
}

// ---------------------------------------------------------------------------
// regenerate — build new meshes, push to the embedded viewer
// ---------------------------------------------------------------------------

void FracplanetMain::regenerate()
{
  meshes.clear();
  mesh_terrain.reset();
  mesh_cloud.reset();

  const clock_t t0 = clock();

  switch (parameters_terrain.object_type)
    {
    case ParametersObject::ObjectTypePlanet:
      {
        std::unique_ptr<TriangleMeshTerrainPlanet> it(
          new TriangleMeshTerrainPlanet(parameters_terrain, this));
        meshes.push_back(it.get());
        mesh_terrain.reset(it.release());
        break;
      }
    default:
      {
        std::unique_ptr<TriangleMeshTerrainFlat> it(
          new TriangleMeshTerrainFlat(parameters_terrain, this));
        meshes.push_back(it.get());
        mesh_terrain.reset(it.release());
        break;
      }
    }

  if (parameters_cloud.enabled)
    {
      switch (parameters_cloud.object_type)
        {
        case ParametersObject::ObjectTypePlanet:
          {
            std::unique_ptr<TriangleMeshCloudPlanet> it(
              new TriangleMeshCloudPlanet(parameters_cloud, this));
            meshes.push_back(it.get());
            mesh_cloud.reset(it.release());
            break;
          }
        default:
          {
            std::unique_ptr<TriangleMeshCloudFlat> it(
              new TriangleMeshCloudFlat(parameters_cloud, this));
            meshes.push_back(it.get());
            mesh_cloud.reset(it.release());
            break;
          }
        }
    }

  const clock_t t1 = clock();

  progress_dialog.reset();

  if (_verbose)
    std::cerr << "Mesh build time: "
              << (t1 - t0) / static_cast<double>(CLOCKS_PER_SEC)
              << "s\n";

  viewer->set_mesh(meshes);

  uint n_tris = 0, n_verts = 0;
  for (const auto* m : meshes)
    if (m) { n_tris += m->triangles(); n_verts += m->vertices(); }

  std::ostringstream ss;
  ss << "Triangles: " << n_tris << "  Vertices: " << n_verts;
  statusBar()->showMessage(ss.str().c_str());
}

// ---------------------------------------------------------------------------
// save_texture
// ---------------------------------------------------------------------------

void FracplanetMain::save_texture()
{
  if (!mesh_terrain)
    {
      QMessageBox::warning(this, "Fracplanet", "No terrain to save — regenerate first.");
      return;
    }

  const uint height = parameters_save.texture_height;
  const uint width  = height * mesh_terrain->geometry().scan_convert_image_aspect_ratio();

  const QString selected_filename = QFileDialog::getSaveFileName(
    this, "Save Texture", ".", "PPM images (*.ppm)");

  if (selected_filename.isEmpty())
    {
      QMessageBox::critical(this, "Fracplanet", "No file specified\nNothing saved");
      return;
    }
  if (!selected_filename.toUpper().endsWith(".PPM"))
    {
      QMessageBox::critical(this, "Fracplanet", "File must have .ppm suffix.");
      return;
    }

  const std::string filename(selected_filename.toLocal8Bit().constData());
  const std::string filename_base(
    selected_filename.left(selected_filename.length() - 4).toLocal8Bit().constData());

  bool ok = true;
  {
    boost::scoped_ptr<Image<ByteRGBA>> terrain_image(new Image<ByteRGBA>(width, height));
    terrain_image->fill(ByteRGBA(0, 0, 0, 0));

    boost::scoped_ptr<Image<ushort>> terrain_dem(new Image<ushort>(width, height));
    terrain_dem->fill(0);

    boost::scoped_ptr<Image<ByteRGBA>> terrain_normals(new Image<ByteRGBA>(width, height));
    terrain_normals->fill(ByteRGBA(128, 128, 128, 0));

    mesh_terrain->render_texture(
      *terrain_image,
      terrain_dem.get(),
      terrain_normals.get(),
      parameters_save.texture_shaded,
      parameters_render.ambient,
      parameters_render.illumination_direction());

    if (!terrain_image->write_ppmfile(filename, this))          ok = false;
    if (ok && !terrain_dem->write_pgmfile(filename_base + "_dem.pgm", this))  ok = false;
    if (ok && !terrain_normals->write_ppmfile(filename_base + "_norm.ppm", this)) ok = false;
  }

  if (ok && mesh_cloud)
    QMessageBox::warning(this, "Fracplanet",
                         "Texture save of cloud mesh is not currently supported.");

  progress_dialog.reset();

  if (!ok)
    QMessageBox::critical(this, "Fracplanet",
                          "Errors occurred while writing texture files.");
}
