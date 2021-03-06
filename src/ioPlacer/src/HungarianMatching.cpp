/////////////////////////////////////////////////////////////////////////////
//
// BSD 3-Clause License
//
// Copyright (c) 2019, University of California, San Diego.
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
//
///////////////////////////////////////////////////////////////////////////////

#include "HungarianMatching.h"

#include "utility/Logger.h"

namespace ppl {

HungarianMatching::HungarianMatching(Section& section,
                                     SlotVector& slots,
                                     Logger* logger)
    : netlist_(section.net), slots_(slots)
{
  num_io_pins_ = netlist_.numIOPins();
  begin_slot_ = section.begin_slot;
  end_slot_ = section.end_slot;
  num_slots_ = end_slot_ - begin_slot_;
  non_blocked_slots_ = section.num_slots;
  edge_ = section.edge;
  logger_ = logger;
}

void HungarianMatching::findAssignment(std::vector<Constraint>& constraints)
{
  createMatrix(constraints);
  hungarian_solver_.solve(hungarian_matrix_, assignment_);
}

void HungarianMatching::createMatrix(std::vector<Constraint>& constraints)
{
  hungarian_matrix_.resize(non_blocked_slots_);
  int slot_index = 0;
  for (int i = begin_slot_; i < end_slot_; ++i) {
    int pinIndex = 0;
    Point newPos = slots_[i].pos;
    if (slots_[i].blocked) {
      continue;
    }
    hungarian_matrix_[slot_index].resize(num_io_pins_);
    netlist_.forEachIOPin([&](int idx, IOPin& io_pin) {
      int hpwl = netlist_.computeIONetHPWL(idx, newPos, edge_, constraints);
      hungarian_matrix_[slot_index][pinIndex] = hpwl;
      pinIndex++;
    });
    slot_index++;
  }
}

inline bool samePos(Point& a, Point& b)
{
  return (a.x() == b.x() && a.y() == b.y());
}

void HungarianMatching::getFinalAssignment(std::vector<IOPin>& assigment) const
{
  size_t rows = non_blocked_slots_;
  size_t col = 0;
  int slot_index = 0;
  netlist_.forEachIOPin([&](int idx, IOPin& io_pin) {
    slot_index = begin_slot_;
    for (size_t row = 0; row < rows; row++) {
      while (slots_[slot_index].blocked && slot_index < slots_.size())
        slot_index++;
      if (assignment_[row] != col) {
        slot_index++;
        continue;
      }
      if (hungarian_matrix_[row][col] == hungarian_fail) {
        logger_->warn(utl::PPL,
                      33,
                      "I/O pin {} cannot be placed in the specified region. "
                      "Not enough space",
                      io_pin.getName().c_str());
      }
      io_pin.setPos(slots_[slot_index].pos);
      io_pin.setLayer(slots_[slot_index].layer);
      assigment.push_back(io_pin);
      Point s_pos = slots_[slot_index].pos;
      for (int i = 0; i < slots_.size(); i++) {
        if (samePos(slots_[i].pos, s_pos)) {
          slots_[i].used = true;
          break;
        }
      }
      break;
    }
    col++;
  });
}

}  // namespace ppl
