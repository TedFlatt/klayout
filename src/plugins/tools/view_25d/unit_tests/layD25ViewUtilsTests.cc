
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2020 Matthias Koefferlein

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/


#include "layD25ViewUtils.h"
#include "tlUnitTest.h"

static std::string v2s (const QVector3D &v)
{
  return tl::to_string (v.x ()) + "," + tl::to_string (v.y ()) + "," + tl::to_string (v.z ());
}

static std::string v2s_2d (const QVector3D &v)
{
  return tl::to_string (v.x ()) + "," + tl::to_string (v.y ());
}

TEST(1_CutPoint)
{
  std::pair<bool, QVector3D> r;

  r = lay::cutpoint_line_with_plane (QVector3D (0, 0, 0), QVector3D (0, 0, 1), QVector3D (0, 0, 0), QVector3D (1, 0, 0));
  EXPECT_EQ (r.first, false);

  r = lay::cutpoint_line_with_plane (QVector3D (1, 2, 3), QVector3D (0, 0, 2), QVector3D (4, 5, 6), QVector3D (0, 0, 1));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "1,2,6");

  r = lay::cutpoint_line_with_plane (QVector3D (1, 2, 3), QVector3D (0, 0, -1), QVector3D (4, 5, 6), QVector3D (1, 1, 1));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "1,2,12");
}

TEST(2_Face)
{
  std::pair<bool, QVector3D> r;

  r = lay::cutpoint_line_with_face (QVector3D (0, 0, 0), QVector3D (0, 0, 1), QVector3D (0, 0, 0), QVector3D (0, 1, 0), QVector3D (0, 0, 1));
  EXPECT_EQ (r.first, false);

  r = lay::cutpoint_line_with_face (QVector3D (1, 2, 3), QVector3D (0, 0, 2), QVector3D (4, 5, 6), QVector3D (0, 1, 0), QVector3D (1, 0, 0));
  EXPECT_EQ (r.first, false);

  r = lay::cutpoint_line_with_face (QVector3D (4, 5, 3), QVector3D (0, 0, 3), QVector3D (4, 5, 6), QVector3D (0, 1, 0), QVector3D (1, 0, 0));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "4,5,6");

  r = lay::cutpoint_line_with_face (QVector3D (4, 7, 3), QVector3D (0, 0, 1), QVector3D (4, 5, 6), QVector3D (0, 1, 0), QVector3D (1, 0, 0));
  EXPECT_EQ (r.first, false);

  r = lay::cutpoint_line_with_face (QVector3D (4, 6, 3), QVector3D (0, 0, 2), QVector3D (4, 5, 6), QVector3D (0, 1, 0), QVector3D (1, 0, 0));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "4,6,6");

  r = lay::cutpoint_line_with_face (QVector3D (5, 6, 3), QVector3D (0, 0, -1), QVector3D (4, 5, 6), QVector3D (0, 1, 0), QVector3D (1, 0, 0));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "5,6,6");

  r = lay::cutpoint_line_with_face (QVector3D (6, 6, 3), QVector3D (0, 0, 1), QVector3D (4, 5, 6), QVector3D (0, 1, 0), QVector3D (1, 0, 0));
  EXPECT_EQ (r.first, false);
}

TEST(3_HitWithCuboid)
{
  std::pair<bool, QVector3D> r;

  r = lay::hit_point_with_cuboid (QVector3D (0, 0, 0), QVector3D (0, 0, 1), QVector3D (-1, -1, 3), QVector3D (2, 2, 2));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "0,0,3");

  r = lay::hit_point_with_cuboid (QVector3D (1, 1, 4), QVector3D (0, 0, 1), QVector3D (-1, -1, 3), QVector3D (2, 2, 2));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "1,1,4");

  r = lay::hit_point_with_cuboid (QVector3D (1, 1, 6), QVector3D (0, 0, 1), QVector3D (-1, -1, 3), QVector3D (2, 2, 2));
  EXPECT_EQ (r.first, false);

  r = lay::hit_point_with_cuboid (QVector3D (5, -6, 0), QVector3D (0, 0, 1), QVector3D (-1, -1, 3), QVector3D (2, 2, 2));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "5,-6,3");

  r = lay::hit_point_with_cuboid (QVector3D (5, -6, 4), QVector3D (0, 0, 1), QVector3D (-1, -1, 3), QVector3D (2, 2, 2));
  EXPECT_EQ (r.first, true);
  EXPECT_EQ (v2s (r.second), "5,-6,5");

  r = lay::hit_point_with_cuboid (QVector3D (5, -6, 6), QVector3D (0, 0, 1), QVector3D (-1, -1, 3), QVector3D (2, 2, 2));
  EXPECT_EQ (r.first, false);
}

TEST(4_CameraNormal)
{
  QMatrix4x4 matrix;
  matrix.perspective (60.0f, 1.5, 0.1f, 100.0f);

  std::pair<QVector3D, QVector3D> ray;
  QVector3D p;

  ray = lay::camera_normal (matrix, 0.0, 0.0);
  EXPECT_EQ (v2s (ray.second.normalized ()), "0,0,-1");

  ray = lay::camera_normal (matrix, 1.0, 0.0);
  EXPECT_EQ (v2s (ray.second), "0.654654,0,-0.755929");

  p = matrix.map (ray.first);
  EXPECT_EQ (v2s_2d (p), "1,0");

  p = matrix.map (ray.first + ray.second);
  EXPECT_EQ (v2s_2d (p), "1,0");

  p = matrix.map (ray.first + ray.second * 1000.0);
  EXPECT_EQ (v2s_2d (p), "1,0");

  ray = lay::camera_normal (matrix, 0.0, -1.0);
  EXPECT_EQ (v2s (ray.second), "0,-0.5,-0.866025");

  p = matrix.map (ray.first);
  EXPECT_EQ (v2s_2d (p), "0,-1");

  p = matrix.map (ray.first + ray.second);
  EXPECT_EQ (v2s_2d (p), "0,-1");

  p = matrix.map (ray.first + ray.second * 1000.0);
  EXPECT_EQ (v2s_2d (p), "0,-1");
}

TEST(5_CameraNormal)
{
  QMatrix4x4 matrix;
  QVector3D p;

  matrix.perspective (60.0f, 1.5, 0.1f, 100.0f);
  matrix.rotate (22.0, 1.0, 0.0, 0.0);
  matrix.rotate (-15.0, 0.0, 1.0, 0.0);
  matrix.translate (QVector3D (0.0, 0.0, 4.0));

  std::pair<QVector3D, QVector3D> ray;

  ray = lay::camera_normal (matrix, 0.0, 1.0);
  EXPECT_EQ (v2s (ray.second), "-0.2563,0.139173,-0.956526");

  p = matrix.map (ray.first);
  EXPECT_EQ (v2s_2d (p), "0,1");

  p = matrix.map (ray.first + ray.second);
  EXPECT_EQ (v2s_2d (p), "0,1");

  p = matrix.map (ray.first + ray.second * 1000.0);
  EXPECT_EQ (v2s_2d (p), "0,1");
}
