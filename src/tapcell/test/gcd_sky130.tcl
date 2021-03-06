source "helpers.tcl"
read_lef sky130/sky130_tech.lef
read_lef sky130/sky130_std_cell.lef
read_def gcd_sky130.def

set def_file [make_result_file gcd_sky130.def]

tapcell -endcap_cpp "2" -distance 15 -tapcell_master "sky130_fd_sc_hs__tap_1" -endcap_master "sky130_fd_sc_hs__decap_4"

write_def $def_file

diff_file gcd_sky130.defok $def_file
