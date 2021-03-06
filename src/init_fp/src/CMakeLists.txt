############################################################################
##
## BSD 3-Clause License
##
## Copyright (c) 2019, James Cherry, Parallax Software, Inc.
## All rights reserved.
##
## Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions are met:
##
## * Redistributions of source code must retain the above copyright notice, this
##   list of conditions and the following disclaimer.
##
## * Redistributions in binary form must reproduce the above copyright notice,
##   this list of conditions and the following disclaimer in the documentation
##   and/or other materials provided with the distribution.
##
## * Neither the name of the copyright holder nor the names of its
##   contributors may be used to endorse or promote products derived from
##   this software without specific prior written permission.
##
## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
## AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
## IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
## ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
## LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
## CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
## SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
## INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
## CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
## ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
## POSSIBILITY OF SUCH DAMAGE.
##
############################################################################

set(INIT_FP_WRAP ${CMAKE_CURRENT_BINARY_DIR}/InitFloorplan_wrap.cc)
set(INIT_FP_TCL_INIT ${CMAKE_CURRENT_BINARY_DIR}/InitFloorplanTclInitVar.cc)

add_custom_command(OUTPUT ${INIT_FP_WRAP}
  COMMAND ${SWIG_EXECUTABLE} -tcl8 -c++ -namespace -prefix ifp -I${INIT_FP_HOME}/include -o ${INIT_FP_WRAP} ${INIT_FP_HOME}/src/InitFloorplan.i
  COMMAND ${OPENSTA_HOME}/etc/SwigCleanup.tcl ${INIT_FP_WRAP}
  WORKING_DIRECTORY ${INIT_FP_HOME}/src
  DEPENDS
  ${INIT_FP_HOME}/src/InitFloorplan.i
  ${INIT_FP_HOME}/include/init_fp/InitFloorplan.hh
  )

add_custom_command(OUTPUT ${INIT_FP_TCL_INIT}
  COMMAND ${OPENSTA_HOME}/etc/TclEncode.tcl ${INIT_FP_TCL_INIT} init_fp_tcl_inits InitFloorplan.tcl
  WORKING_DIRECTORY ${INIT_FP_HOME}/src
  DEPENDS InitFloorplan.tcl
  )

add_library(init_fp
  InitFloorplan.cc
  MakeInitFloorplan.cc
  ${INIT_FP_WRAP}
  ${INIT_FP_TCL_INIT}
  )

target_include_directories(init_fp
  PUBLIC ${INIT_FP_HOME}/include
  PRIVATE
  ${OPENROAD_HOME}/include
  ${OPENSTA_HOME}/include
  ${DBSTA_HOME}/include
  ${OPENDB_HOME}/include
  ${OPENSTA_INCLUDE_DIRS}
  ${TCL_INCLUDE_PATH}
  )

target_link_libraries(init_fp
  PUBLIC
  utility
  )
