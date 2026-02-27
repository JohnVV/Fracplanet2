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

#include "control_save.h"

#include "fracplanet_main.h"

ControlSave::ControlSave(FracplanetMain* save_target,ParametersSave* param)
  :Control()
  ,parameters(param)
{
  QWidget*const tab_texture=new QWidget();
  tab_texture->setLayout(new QVBoxLayout());
  layout()->addWidget(tab_texture);

  QCheckBox*const shaded_checkbox=new QCheckBox("Shaded texture");
  tab_texture->layout()->addWidget(shaded_checkbox);
  shaded_checkbox->setChecked(parameters->texture_shaded);
  shaded_checkbox->setToolTip("Check to have the texture include relief shading");
  connect(
	  shaded_checkbox,SIGNAL(checkStateChanged(Qt::CheckState)),
	  this,SLOT(setTextureShaded(Qt::CheckState))
	  );

  QWidget*const grid_texture=new QWidget();
  tab_texture->layout()->addWidget(grid_texture);
  QGridLayout* grid_layout=new QGridLayout();
  grid_texture->setLayout(grid_layout);

  grid_layout->addWidget(new QLabel("Texture height",grid_texture),0,0);
  QSpinBox* texture_height_spinbox=new QSpinBox();
  grid_layout->addWidget(texture_height_spinbox,0,1);
  texture_height_spinbox->setMinimum(1);
  texture_height_spinbox->setMaximum(0x7fffffff);
  texture_height_spinbox->setValue(1024);
  texture_height_spinbox->setToolTip("Texture height in pixels; the texture width is the same as the height\nexcept for spherical geometry (planets) when it is double.");
  connect(
	  texture_height_spinbox,SIGNAL(valueChanged(int)),
	  this,SLOT(setTextureHeight(int))
	  );

  QPushButton*const save_texture=new QPushButton("Save as texture");
  tab_texture->layout()->addWidget(save_texture);
  save_texture->setToolTip("Press to save object as textures");
  connect(
	  save_texture,SIGNAL(clicked()),
	  save_target,SLOT(save_texture())
	  );
}

ControlSave::~ControlSave()
{}

void ControlSave::setTextureShaded(Qt::CheckState v)
{
  parameters->texture_shaded=(v==Qt::Checked);
}

void ControlSave::setTextureHeight(int v)
{
  parameters->texture_height=v;
}
