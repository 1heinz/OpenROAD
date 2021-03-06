/////////////////////////////////////////////////////////////////////////////
// Original authors: SangGi Do(sanggido@unist.ac.kr), Mingyu
// Woo(mwoo@eng.ucsd.edu)
//          (respective Ph.D. advisors: Seokhyeong Kang, Andrew B. Kahng)
// Rewrite by James Cherry, Parallax Software, Inc.
//
// Copyright (c) 2019, OpenROAD
// Copyright (c) 2018, SangGi Do and Mingyu Woo
// All rights reserved.
//
// BSD 3-Clause License
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

#include "opendp/Opendp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "utility/Logger.h"
#include "openroad/OpenRoad.hh"

namespace dpl {

using std::abs;
using std::max;
using std::min;
using std::numeric_limits;
using std::sort;
using std::string;
using std::vector;

using ord::closestPtInRect;
using utl::DPL;

static bool
cellAreaLess(const Cell *cell1, const Cell *cell2);

void
Opendp::detailedPlacement()
{
  initGrid();
  if (!groups_.empty()) {
    placeGroups();
  }
  place();
}

// Find the location inside the core and not on top of a macro to start
// placement.
//
// This is preferable to using a the diamond search because the max_displacement
// would have to know the macro size and no search is necessary.
// Note that this does not need to consider padding because its only job is to
// get the overlapping cells close to the edge of the block so that the normal
// diamond search can do it's job.
void
Opendp::prePlaceLocation(const Cell *cell,
                         bool padded,
                         // Return values.
                         int *x,
                         int *y) const
{
  int pre_x, pre_y;
  initialLocation(cell, padded, &pre_x, &pre_y);

  // Move inside core.
  pre_x = min(max(0, pre_x), row_site_count_ * site_width_);
  pre_y = min(max(0, pre_y), row_count_ * row_height_);

  // Move std cells off of macros.
  int grid_x = gridX(pre_x);
  int grid_y = gridY(pre_y);
  Pixel *pixel = gridPixel(grid_x, grid_y);
  if (pixel) {
    const Cell *block = pixel->cell;
    if (block
        && isBlock(block)) {
      Rect block_bbox(block->x_, block->y_,
                      block->x_ + block->width_, block->y_ + block->height_);
      if (pre_x >= block_bbox.xMin()
          && pre_x <= block_bbox.xMax()
          && pre_y >= block_bbox.yMin()
          && pre_y <= block_bbox.yMax()) {
        int x_dist_min = pre_x - block_bbox.xMin();
        int x_dist_max = block_bbox.xMax() - pre_x;
        int y_dist_min = pre_y - block_bbox.yMin();
        int y_dist_max = block_bbox.yMax() - pre_y;
        if (x_dist_min < x_dist_max
            && x_dist_min < y_dist_min
            && x_dist_min < y_dist_max) {
          pre_x = block_bbox.xMin() - cell->width_;
        }
        else if (x_dist_max <= x_dist_min
                 && x_dist_max <= y_dist_min
                 && x_dist_max <= y_dist_max) {
          pre_x = block_bbox.xMax();
        }
        else if (y_dist_min <= x_dist_min
                 && y_dist_min <= x_dist_max
                 && y_dist_min <= y_dist_max) {
          pre_y = block_bbox.yMin() - cell->height_;
        }
        else if (y_dist_max <= x_dist_min
                 && y_dist_max <= x_dist_max
                 && y_dist_max <= y_dist_min) {
          pre_y = block_bbox.yMax();
        }
      }
    }
  }
  *x = pre_x;
  *y = pre_y;
}

void
Opendp::placeGroups()
{
  group_cell_region_assign();

  prePlaceGroups();
  prePlace();

  // naive placement method ( multi -> single )
  placeGroups2();
  for (Group &group : groups_) {
    // magic number alert
    for (int pass = 0; pass < 3; pass++) {
      int refine_count = groupRefine(&group);
      int anneal_count = anneal(&group);
      // magic number alert
      if (refine_count < 10 || anneal_count < 100) {
        break;
      }
    }
  }
}

void
Opendp::prePlace()
{
  for (Cell &cell : cells_) {
    bool in_group = false;
    Rect *target;
    if (!cell.inGroup() && !cell.is_placed_) {
      for (Group &group : groups_) {
        for (Rect &rect : group.regions) {
          if (check_overlap(&cell, &rect)) {
            in_group = true;
            target = &rect;
          }
        }
      }
      if (in_group) {
        Point nearest = nearestPt(&cell, target);
        if (map_move(&cell, nearest.x(), nearest.y())) {
          cell.hold_ = true;
        }
      }
    }
  }
}

/* static */
bool
Opendp::check_overlap(const Cell *cell, const Rect *rect) const
{
  int x, y;
  prePlaceLocation(cell, true, &x, &y);

  return x + paddedWidth(cell) > rect->xMin() && x < rect->xMax() && y + cell->height_ > rect->yMin() && y < rect->yMax();
}

Point
Opendp::nearestPt(const Cell *cell, const Rect *rect) const
{
  int x, y;
  prePlaceLocation(cell, false, &x, &y);
  int temp_x = x;
  int temp_y = y;

  if (check_overlap(cell, rect)) {
    int dist_x = 0;
    int dist_y = 0;
    if (abs(x - rect->xMin() + paddedWidth(cell)) > abs(rect->xMax() - x)) {
      dist_x = abs(rect->xMax() - x);
      temp_x = rect->xMax();
    }
    else {
      dist_x = abs(x - rect->xMin());
      temp_x = rect->xMin() - paddedWidth(cell);
    }
    if (abs(y - rect->yMin() + cell->height_) > abs(rect->yMax() - y)) {
      dist_y = abs(rect->yMax() - y);
      temp_y = rect->yMax();
    }
    else {
      dist_y = abs(y - rect->yMin());
      temp_y = rect->yMin() - cell->height_;
    }
    assert(dist_x >= 0);
    assert(dist_y >= 0);
    if (dist_x < dist_y) {
      return Point(temp_x, y);
    }
    return Point(x, temp_y);
  }

  if (x < rect->xMin()) {
    temp_x = rect->xMin();
  }
  else if (x + paddedWidth(cell) > rect->xMax()) {
    temp_x = rect->xMax() - paddedWidth(cell);
  }

  if (y < rect->yMin()) {
    temp_y = rect->yMin();
  }
  else if (y + cell->height_ > rect->yMax()) {
    temp_y = rect->yMax() - cell->height_;
  }

  return Point(temp_x, temp_y);
}

void
Opendp::prePlaceGroups()
{
  for (Group &group : groups_) {
    for (Cell *cell : group.cells_) {
      if (!(isFixed(cell) || cell->is_placed_)) {
        int dist = numeric_limits<int>::max();
        bool in_group = false;
        Rect *target = nullptr;
        for (Rect &rect : group.regions) {
          if (check_inside(cell, &rect)) {
            in_group = true;
          }
          int temp_dist = dist_for_rect(cell, &rect);
          if (temp_dist < dist) {
            dist = temp_dist;
            target = &rect;
          }
        }
        if (!in_group) {
          Point nearest = nearestPt(cell, target);
          if (map_move(cell, nearest.x(), nearest.y())) {
            cell->hold_ = true;
          }
        }
      }
    }
  }
}

bool
Opendp::check_inside(const Cell *cell, const Rect *rect) const
{
  int x, y;
  prePlaceLocation(cell, true, &x, &y);
  return x >= rect->xMin() && x + paddedWidth(cell) <= rect->xMax() && y >= rect->yMin() && y + cell->height_ <= rect->yMax();
}

int
Opendp::dist_for_rect(const Cell *cell, const Rect *rect) const
{
  int x, y;
  prePlaceLocation(cell, true, &x, &y);
  int dist_x = 0;
  int dist_y = 0;

  if (x < rect->xMin()) {
    dist_x = rect->xMin() - x;
  }
  else if (x + paddedWidth(cell) > rect->xMax()) {
    dist_x = x + paddedWidth(cell) - rect->xMax();
  }

  if (y < rect->yMin()) {
    dist_y = rect->yMin() - y;
  }
  else if (y + cell->height_ > rect->yMax()) {
    dist_y = y + cell->height_ - rect->yMax();
  }

  assert(dist_y >= 0);
  assert(dist_x >= 0);

  return dist_y + dist_x;
}

void
Opendp::place()
{
  vector<Cell *> sorted_cells;
  sorted_cells.reserve(cells_.size());

  for (Cell &cell : cells_) {
    if (!(isFixed(&cell) || cell.inGroup() || cell.is_placed_)) {
      sorted_cells.push_back(&cell);
    }
  }
  sort(sorted_cells.begin(), sorted_cells.end(), cellAreaLess);

  // Place multi-row instances first.
  if (have_multi_height_cells_) {
    for (Cell *cell : sorted_cells) {
      if (isMultiRow(cell) && cellFitsInCore(cell)) {
        if (!map_move(cell)) {
          shift_move(cell);
        }
      }
    }
  }
  for (Cell *cell : sorted_cells) {
    if (cellFitsInCore(cell)) {
      if (!isMultiRow(cell)) {
        if (!map_move(cell)) {
          shift_move(cell);
        }
      }
    }
    else {
      logger_->warn(DPL, 15, "instance {} does not fit inside the ROW core area.",
                    cell->name());
    }
  }
  // This has negligible benefit -cherry
  // anneal();
}

bool
Opendp::cellFitsInCore(Cell *cell)
{
  return gridPaddedWidth(cell) <= row_site_count_ && gridHeight(cell) <= row_count_;
}

static bool
cellAreaLess(const Cell *cell1, const Cell *cell2)
{
  int area1 = cell1->area();
  int area2 = cell2->area();
  if (area1 > area2) {
    return true;
  }
  if (area1 < area2) {
    return false;
  }
  return cell1->db_inst_->getId() < cell2->db_inst_->getId();
}

void
Opendp::placeGroups2()
{
  for (Group &group : groups_) {
    bool single_pass = true;
    bool multi_pass = true;
    vector<Cell *> group_cells;
    group_cells.reserve(cells_.size());
    for (Cell *cell : group.cells_) {
      if (!isFixed(cell) && !cell->is_placed_) {
        group_cells.push_back(cell);
      }
    }
    sort(group_cells.begin(), group_cells.end(), cellAreaLess);
    // Place multi-row cells on each group region.
    for (Cell *cell : group_cells) {
      if (!isFixed(cell) && !cell->is_placed_) {
        assert(cell->inGroup());
        if (isMultiRow(cell)) {
          multi_pass = map_move(cell);
          if (!multi_pass) {
            break;
          }
        }
      }
    }
    if (multi_pass) {
      // Place single-row cells in each group region.
      for (Cell *cell : group_cells) {
        if (!isFixed(cell) && !cell->is_placed_) {
          assert(cell->inGroup());
          if (!isMultiRow(cell)) {
            single_pass = map_move(cell);
            if (!single_pass) {
              break;
            }
          }
        }
      }
    }

    if (!single_pass || !multi_pass) {
      // Erase group cells
      for (Cell *cell : group.cells_) {
        erase_pixel(cell);
      }

      // determine brick placement by utilization
      // magic number alert
      if (group.util > 0.95) {
        brickPlace1(&group);
      }
      else {
        brickPlace2(&group);
      }
    }
  }
}

// place toward group edges
void
Opendp::brickPlace1(const Group *group)
{
  const Rect *boundary = &group->boundary;
  vector<Cell *> sort_by_dist(group->cells_);

  sort(sort_by_dist.begin(), sort_by_dist.end(), [&](Cell *cell1, Cell *cell2) {
    return rectDist(cell1, boundary) < rectDist(cell2, boundary);
  });

  for (Cell *cell : sort_by_dist) {
    int x, y;
    rectDist(cell, boundary, &x, &y);

    bool valid = map_move(cell, x, y);
    if (!valid) {
      logger_->warn(DPL, 16, "cannot place instance (brick place 1) {}.",
                    cell->name());
    }
  }
}

void
Opendp::rectDist(const Cell *cell,
                 const Rect *rect,
                 // Return values.
                 int *x,
                 int *y) const
{
  *x = 0;
  *y = 0;
  int init_x, init_y;
  prePlaceLocation(cell, false, &init_x, &init_y);

  if (init_x > (rect->xMin() + rect->xMax()) / 2) {
    *x = rect->xMax();
  }
  else {
    *x = rect->xMin();
  }

  if (init_y > (rect->yMin() + rect->yMax()) / 2) {
    *y = rect->yMax();
  }
  else {
    *y = rect->yMin();
  }
}

int
Opendp::rectDist(const Cell *cell, const Rect *rect) const
{
  int x, y;
  rectDist(cell, rect, &x, &y);
  int init_x, init_y;
  prePlaceLocation(cell, false, &init_x, &init_y);
  return abs(init_x - x) + abs(init_y - y);
}

// place toward region edges
void
Opendp::brickPlace2(const Group *group)
{
  vector<Cell *> sort_by_dist(group->cells_);

  sort(sort_by_dist.begin(), sort_by_dist.end(), [&](Cell *cell1, Cell *cell2) {
    return rectDist(cell1, cell1->region_) < rectDist(cell2, cell2->region_);
  });

  for (Cell *cell : sort_by_dist) {
    if (!cell->hold_) {
      int x, y;
      rectDist(cell, cell->region_, &x, &y);
      bool valid = map_move(cell, x, y);
      if (!valid)
        logger_->warn(DPL, 17, "cannot place instance (brick place 2) {}.",
                      cell->name());
    }
  }
}

int
Opendp::groupRefine(const Group *group)
{
  vector<Cell *> sort_by_disp(group->cells_);

  sort(sort_by_disp.begin(), sort_by_disp.end(), [&](Cell *cell1, Cell *cell2) {
    return (disp(cell1) > disp(cell2));
  });

  int count = 0;
  for (int i = 0; i < sort_by_disp.size() * group_refine_percent_; i++) {
    Cell *cell = sort_by_disp[i];
    if (!cell->hold_) {
      if (refine_move(cell)) {
        count++;
      }
    }
  }
  return count;
}

// This is NOT annealing. It is random swapping. -cherry
int
Opendp::anneal(Group *group)
{
  srand(rand_seed_);
  int count = 0;

  // magic number alert
  for (int i = 0; i < 100 * group->cells_.size(); i++) {
    Cell *cell1 = group->cells_[rand() % group->cells_.size()];
    Cell *cell2 = group->cells_[rand() % group->cells_.size()];
    if (swap_cell(cell1, cell2)) {
      count++;
    }
  }
  return count;
}

// This is NOT annealing. It is random swapping. -cherry
int
Opendp::anneal()
{
  srand(rand_seed_);
  int count = 0;
  // magic number alert
  for (int i = 0; i < 100 * cells_.size(); i++) {
    Cell *cell1 = &cells_[rand() % cells_.size()];
    Cell *cell2 = &cells_[rand() % cells_.size()];
    if (swap_cell(cell1, cell2)) {
      count++;
    }
  }
  return count;
}

// Not called -cherry.
int
Opendp::refine()
{
  vector<Cell *> sorted;
  sorted.reserve(cells_.size());

  for (Cell &cell : cells_) {
    if (!(isFixed(&cell) || cell.hold_ || cell.inGroup())) {
      sorted.push_back(&cell);
    }
  }
  sort(sorted.begin(), sorted.end(), [&](Cell *cell1, Cell *cell2) {
    return disp(cell1) > disp(cell2);
  });

  int count = 0;
  for (int i = 0; i < sorted.size() * refine_percent_; i++) {
    Cell *cell = sorted[i];
    if (!cell->hold_) {
      if (refine_move(cell)) {
        count++;
      }
    }
  }
  return count;
}

////////////////////////////////////////////////////////////////

bool
Opendp::map_move(Cell *cell)
{
  int init_x, init_y;
  prePlaceLocation(cell, true, &init_x, &init_y);
  return map_move(cell, init_x, init_y);
}

bool
Opendp::map_move(Cell *cell, int x, int y)
{
  Pixel *pixel = diamondSearch(cell, x, y);
  if (pixel) {
    Pixel *near_pixel = diamondSearch(
        cell,
        pixel->grid_x_ * site_width_,
        pixel->grid_y_ * row_height_);
    if (near_pixel) {
      paint_pixel(cell, near_pixel->grid_x_, near_pixel->grid_y_);
    }
    else {
      paint_pixel(cell, pixel->grid_x_, pixel->grid_y_);
    }
    return true;
  }
  return false;
}

bool
Opendp::shift_move(Cell *cell)
{
  int x, y;
  prePlaceLocation(cell, true, &x, &y);
  int grid_x = gridX(x);
  int grid_y = gridY(y);
  // magic number alert
  int boundary_margin = 3;
  int margin_width = paddedWidth(cell) * boundary_margin;
  set<Cell *> region_cells;
  for (int x = grid_x - margin_width; x < grid_x + margin_width; x++) {
    for (int y = grid_y - boundary_margin; y < grid_y + boundary_margin; y++) {
      Pixel *pixel = gridPixel(x, y);
      if (pixel) {
        Cell *cell = pixel->cell;
        if (cell && !isFixed(cell))
          region_cells.insert(cell);
      }
    }
  }

  // erase region cells
  for (Cell *around_cell : region_cells) {
    if (cell->inGroup() == around_cell->inGroup()) {
      erase_pixel(around_cell);
    }
  }

  // place target cell
  if (!map_move(cell, x, y)) {
    logger_->warn(DPL, 18, "detailed placement failed on {}.",
                  cell->name());
    return false;
  }

  // re-place erased cells
  for (Cell *around_cell : region_cells) {
    if (cell->inGroup() == around_cell->inGroup()) {
      if (!map_move(around_cell)) {
        logger_->warn(DPL, 19, "detailed placement failed on {}",
                      around_cell->name());
        return false;
      }
    }
  }
  return true;
}

bool
Opendp::swap_cell(Cell *cell1, Cell *cell2)
{
  if (cell1 != cell2
      && !cell1->hold_
      && !cell2->hold_
      && cell1->width_ == cell2->width_
      && cell1->height_ == cell2->height_
      && !isFixed(cell1)
      && !isFixed(cell2)) {
    int dist_change = distChange(cell1, cell2->x_, cell2->y_)
      + distChange(cell2, cell1->x_, cell1->y_);

    if (dist_change < 0) {
      int grid_x1 = gridPaddedX(cell2);
      int grid_y1 = gridY(cell2);
      int grid_x2 = gridPaddedX(cell1);
      int grid_y2 = gridY(cell1);

      erase_pixel(cell1);
      erase_pixel(cell2);
      paint_pixel(cell1, grid_x1, grid_y1);
      paint_pixel(cell2, grid_x2, grid_y2);
      return true;
    }
  }
  return false;
}

bool
Opendp::refine_move(Cell *cell)
{
  int init_x, init_y;
  prePlaceLocation(cell, false, &init_x, &init_y);
  Pixel *pixel = diamondSearch(cell, init_x, init_y);
  if (pixel) {
    double dist = abs(init_x - pixel->grid_x_ * site_width_) + abs(init_y - pixel->grid_y_ * row_height_);
    if (max_displacement_constraint_ != 0 && (dist / row_height_ > max_displacement_constraint_)) {
      return false;
    }

    int dist_change = distChange(cell,
                                 pixel->grid_x_ * site_width_,
                                 pixel->grid_y_ * row_height_);

    if (dist_change < 0) {
      erase_pixel(cell);
      paint_pixel(cell, pixel->grid_x_, pixel->grid_y_);
      return true;
    }
    return false;
  }
  return false;
}

int
Opendp::distChange(const Cell *cell, int x, int y) const
{
  int init_x, init_y;
  prePlaceLocation(cell, false, &init_x, &init_y);
  int curr_dist = abs(cell->x_ - init_x) + abs(cell->y_ - init_y);
  int new_dist = abs(init_x - x) + abs(init_y - y);
  return new_dist - curr_dist;
}

////////////////////////////////////////////////////////////////

Pixel *
Opendp::diamondSearch(const Cell *cell, int x, int y) const
{
  int grid_x = gridX(x);
  int grid_y = gridY(y);

  // Restrict check to group region.
  Group *group = cell->group_;
  if (group) {
    Rect grid_boundary(divCeil(group->boundary.xMin(), site_width_),
                       divCeil(group->boundary.yMin(), row_height_),
                       group->boundary.xMax() / site_width_,
                       group->boundary.yMax() / row_height_);
    Point in_boundary = closestPtInRect(grid_boundary, Point(grid_x, grid_y));
    grid_x = in_boundary.x();
    grid_y = in_boundary.y();
  }

  // Restrict check to core.
  Rect grid_core(0,
                 0,
                 row_site_count_ - gridPaddedWidth(cell),
                 row_count_ - gridHeight(cell));
  Point in_core = closestPtInRect(grid_core, Point(grid_x, grid_y));
  int avail_x, avail_y;
  if (binSearch(grid_x, cell, in_core.x(), in_core.y(), &avail_x, &avail_y)) {
    return &grid_[avail_y][avail_x];
  }

  // Set search boundary max / min
  Point start = closestPtInRect(
      grid_core,
      Point(grid_x - diamond_search_width_, grid_y - diamond_search_height_));
  Point end = closestPtInRect(
      grid_core,
      Point(grid_x + diamond_search_width_, grid_y + diamond_search_height_));
  ;
  int x_start = start.x();
  int y_start = start.y();
  int x_end = end.x();
  int y_end = end.y();

#ifdef ODP_DEBUG
  cout << " == Start Diamond Search ==  " << endl;
  cout << " cell_name : " << cell->name() << endl;
  cout << " x : " << x << endl;
  cout << " y : " << y << endl;
  cout << " grid_x : " << grid_x << endl;
  cout << " grid_y : " << grid_y << endl;
  cout << " x bound ( " << x_start << ") - (" << x_end << ")" << endl;
  cout << " y bound ( " << y_start << ") - (" << y_end << ")" << endl;
#endif

  for (int i = 1; i < diamond_search_height_; i++) {
    Pixel *pixel = nullptr;
    int best_dist = 0;
    // right side
    for (int j = 1; j < i * 2; j++) {
      int x_offset = -((j + 1) / 2);
      int y_offset = (i * 2 - j) / 2;
      if (j % 2 == 1) {
        y_offset = -y_offset;
      }
      if (binSearch(
              grid_x,
              cell,
              min(x_end, max(x_start, grid_x + x_offset * bin_search_width_)),
              min(y_end, max(y_start, grid_y + y_offset)),
              &avail_x,
              &avail_y)) {
        Pixel *avail = &grid_[avail_y][avail_x];
        int avail_dist = abs(x - avail->grid_x_ * site_width_) + abs(y - avail->grid_y_ * row_height_);
        if (pixel == nullptr || avail_dist < best_dist) {
          pixel = avail;
          best_dist = avail_dist;
        }
      }
    }

    // left side
    for (int j = 1; j < (i + 1) * 2; j++) {
      int x_offset = (j - 1) / 2;
      int y_offset = ((i + 1) * 2 - j) / 2;
      if (j % 2 == 1) {
        y_offset = -y_offset;
      }
      if (binSearch(
              grid_x,
              cell,
              min(x_end, max(x_start, grid_x + x_offset * bin_search_width_)),
              min(y_end, max(y_start, grid_y + y_offset)),
              &avail_x,
              &avail_y)) {
        Pixel *avail = &grid_[avail_y][avail_x];
        int avail_dist = abs(x - avail->grid_x_ * site_width_) + abs(y - avail->grid_y_ * row_height_);
        if (pixel == nullptr || avail_dist < best_dist) {
          pixel = avail;
          best_dist = avail_dist;
        }
      }
    }
    if (pixel) {
      return pixel;
    }
  }
  return nullptr;
}

bool
Opendp::binSearch(int grid_x,
                  const Cell *cell,
                  int x,
                  int y,
                  // Return values
                  int *avail_x,
                  int *avail_y) const
{
  int x_end = x + gridPaddedWidth(cell);
  int height = gridHeight(cell);
  int y_end = y + height;

  // Check y is beyond the border.
  if (y_end > coreGridMaxY()
      // Check top power for even row multi-deck cell.
      || (height % 2 == 0 && rowTopPower(y) == topPower(cell))) {
    return false;
  }

#ifdef ODP_DEBUG
  cout << " - - - - - - - - - - - - - - - - - " << endl;
  cout << " Start Bin Search " << endl;
  cout << " cell name : " << cell->name() << endl;
  cout << " target x : " << x << endl;
  cout << " target y : " << y << endl;
#endif
  if (grid_x > x) {
    for (int i = bin_search_width_ - 1; i >= 0; i--) {
      // check all grids are empty
      bool available = true;

      if (x_end + i > coreGridMaxX()) {
        available = false;
      }
      else {
        for (int k = y; k < y_end; k++) {
          for (int l = x + i; l < x_end + i; l++) {
            Pixel &pixel = grid_[k][l];
            if (pixel.cell || !pixel.is_valid
                // check group regions
                || (cell->inGroup() && pixel.group_ != cell->group_) || (!cell->inGroup() && pixel.group_)) {
              available = false;
              break;
            }
          }
          if (!available) {
            break;
          }
        }
      }
      if (available) {
        *avail_x = x + i;
        *avail_y = y;
        return true;
      }
    }
  }
  else {
    for (int i = 0; i < bin_search_width_; i++) {
      // check all grids are empty
      bool available = true;
      if (x_end + i > coreGridMaxX()) {
        available = false;
      }
      else {
        for (int k = y; k < y_end; k++) {
          for (int l = x + i; l < x_end + i; l++) {
            Pixel &pixel = grid_[k][l];
            if (pixel.cell || !pixel.is_valid) {
              available = false;
              break;
            }
            // check group regions
            if (cell->inGroup()) {
              if (pixel.group_ != cell->group_) {
                available = false;
                break;
              }
            }
            else {
              if (pixel.group_) {
                available = false;
                break;
              }
            }
          }
        }
      }
      if (available) {
        *avail_x = x + i;
        *avail_y = y;
        return true;
      }
    }
  }
  return false;
}

}  // namespace opendp
