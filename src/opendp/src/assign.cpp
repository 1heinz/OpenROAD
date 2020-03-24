/////////////////////////////////////////////////////////////////////////////
// Original authors: SangGi Do(sanggido@unist.ac.kr), Mingyu Woo(mwoo@eng.ucsd.edu)
//          (respective Ph.D. advisors: Seokhyeong Kang, Andrew B. Kahng)
// Rewrite by James Cherry, Parallax Software, Inc.

// BSD 3-Clause License
//
// Copyright (c) 2019, James Cherry, Parallax Software, Inc.
// Copyright (c) 2018, SangGi Do and Mingyu Woo
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
///////////////////////////////////////////////////////////////////////////////

#include <cmath>
#include <limits>
#include "openroad/Error.hh"
#include "opendp/Opendp.h"

namespace opendp {

using std::cerr;
using std::cout;
using std::endl;
using std::max;
using std::min;
using std::abs;
using std::round;
using std::numeric_limits;

using ord::error;

void Opendp::fixed_cell_assign() {
  for(Cell &cell : cells_) {
    if(isFixed(&cell)) {
      int y_start = gridY(&cell);
      int y_end = gridEndY(&cell);
      int x_start = gridPaddedX(&cell);
      int x_end = gridPaddedEndX(&cell);

      int y_start_rf = 0;
      int y_end_rf = gridEndY();
      int x_start_rf = 0;
      int x_end_rf = gridEndX();

      y_start = max(y_start, y_start_rf);
      y_end = min(y_end, y_end_rf);
      x_start = max(x_start, x_start_rf);
      x_end = min(x_end, x_end_rf);

#ifdef ODP_DEBUG
      cout << "FixedCellAssign: cell_name : "
           << cell.name() << endl;
      cout << "FixedCellAssign: y_start : " << y_start << endl;
      cout << "FixedCellAssign: y_end   : " << y_end << endl;
      cout << "FixedCellAssign: x_start : " << x_start << endl;
      cout << "FixedCellAssign: x_end   : " << x_end << endl;
#endif
      for(int j = y_start; j < y_end; j++) {
        for(int k = x_start; k < x_end; k++) {
          grid_[j][k].cell = &cell;
          grid_[j][k].util = 1.0;
        }
      }
    }
  }
}

void Opendp::group_cell_region_assign() {
  for(Group& group : groups_) {
    int64_t site_count = 0;
    for(int j = 0; j < row_count_; j++) {
      for(int k = 0; k < row_site_count_; k++) {
	if(grid_[j][k].is_valid
	   && grid_[j][k].group_ == &group)
	  site_count++;
      }
    }
    int64_t area = site_count * site_width_ * row_height_;

    int64_t cell_area = 0;
    for(Cell* cell : group.cells_) {
      cell_area += cell->area();

      for(Rect &rect : group.regions) {
	if (check_inside(cell, &rect))
	  cell->region_ = &rect;
      }
      if(cell->region_ == nullptr)
	cell->region_ = &group.regions[0];
    }
    group.util = static_cast<double>(cell_area) / area;
  }
}

void Opendp::group_pixel_assign2() {
  for(int i = 0; i < row_count_; i++) {
    for(int j = 0; j < row_site_count_; j++) {
      Rect grid2;
      grid2.init(j * site_width_, i * row_height_,
		(j + 1) * site_width_, (i + 1) * row_height_);
      for(Group& group : groups_) {
        for(Rect &rect : group.regions) {
	  if(!check_inside(grid2, rect) &&
             check_overlap(grid2, rect)) {
            grid_[i][j].util = 0.0;
            grid_[i][j].cell = &dummy_cell_;
            grid_[i][j].is_valid = false;
          }
        }
      }
    }
  }
}

void Opendp::group_pixel_assign() {
  for(int i = 0; i < row_count_; i++) {
    for(int j = 0; j < row_site_count_; j++) {
      grid_[i][j].util = 0.0;
    }
  }

  for(Group& group : groups_) {
    for(Rect &rect : group.regions) {
      int row_start = divCeil(rect.yMin(), row_height_);
      int row_end = divFloor(rect.yMax(), row_height_);

      for(int k = row_start; k < row_end; k++) {
        int col_start = divCeil(rect.xMin(), site_width_);
        int col_end = divFloor(rect.xMax(), site_width_);

        for(int l = col_start; l < col_end; l++) {
          grid_[k][l].util += 1.0;
        }
        if(rect.xMin() % site_width_ != 0) {
          grid_[k][col_start].util -=
	    (rect.xMin() % site_width_) / static_cast<double>(site_width_);
        }
        if(rect.xMax() % site_width_ != 0) {
          grid_[k][col_end - 1].util -=
	    ((site_width_ - rect.xMax()) % site_width_) / static_cast<double>(site_width_);
        }
      }
    }
    for(Rect& rect : group.regions) {
      int row_start = divCeil(rect.yMin(), row_height_);
      int row_end = divFloor(rect.yMax(), row_height_);

      for(int k = row_start; k < row_end; k++) {
        int col_start = divCeil(rect.xMin(), site_width_);
        int col_end = divFloor(rect.xMax(), site_width_);

        // Assign group to each pixel.
        for(int l = col_start; l < col_end; l++) {
          if(grid_[k][l].util == 1.0) {
            grid_[k][l].group_ = &group;
	    grid_[k][l].is_valid = true;
            grid_[k][l].util = 1.0;
	  }
          else if(grid_[k][l].util > 0.0 && grid_[k][l].util < 1.0) {
            grid_[k][l].cell = &dummy_cell_;
            grid_[k][l].util = 0.0;
            grid_[k][l].is_valid = false;
          }
        }
      }
    }
  }
}

void Opendp::erase_pixel(Cell* cell) {
  if(!(isFixed(cell) || !cell->is_placed_)) {
    int x_step = gridPaddedWidth(cell);
    int y_step = gridHeight(cell);
    for(int i = gridY(cell); i < gridY(cell) + y_step; i++) {
      for(int j = gridPaddedX(cell); j < gridPaddedX(cell) + x_step; j++) {
	grid_[i][j].cell = nullptr;
	grid_[i][j].util = 0;
      }
    }
    cell->x_ = 0;
    cell->y_ = 0;
    cell->is_placed_ = false;
    cell->hold_ = false;
  }
}

void Opendp::paint_pixel(Cell* cell, int grid_x, int grid_y) {
  assert(!cell->is_placed_);
  int x_step = gridPaddedWidth(cell);
  int y_step = gridHeight(cell);

  setGridPaddedLoc(cell, grid_x, grid_y);
  cell->is_placed_ = true;

#ifdef ODP_DEBUG
  cout << "paint cell : " << cell->name() << endl;
  cout << "x_ - y_ : " << cell->x_ << " - "
       << cell->y_ << endl;
  cout << "x_step - y_step : " << x_step << " - " << y_step << endl;
  cout << "grid_x - grid_y : " << grid_x << " - " << grid_y << endl;
#endif

  for(int i = grid_y; i < grid_y + y_step; i++) {
    for(int j = grid_x; j < grid_x + x_step; j++) {
      if(grid_[i][j].cell != nullptr) {
        error("Cannot paint grid because it is already occupied.");
      }
      else {
        grid_[i][j].cell = cell;
        grid_[i][j].util = 1.0;
      }
    }
  }
  if(max_cell_height_ > 1) {
    if(y_step % 2 == 1) {
      if(rowTopPower(grid_y) != topPower(cell))
        cell->orient_ = dbOrientType::MX;
      else
        cell->orient_ = dbOrientType::R0;
    }
  }
  else {
    cell->orient_ = rowOrient(grid_y);
  }
}

}  // namespace opendp
