###############################################################################
# Copyright (c) 2016 - 2020 Xilinx, Inc.  All rights reserved.
# SPDX-License-Identifier: MIT
#
# Modification History
#
# Ver   Who  Date     Changes
# ----- ---- -------- -----------------------------------------------
# 1.00  srt  05/16/16 Initial Release
##############################################################################

#---------------------------------------------
# libmetal_drc
#---------------------------------------------
proc libmetal_drc {libhandle} {
    # check processor type
    set proc_instance [hsi::get_sw_processor];
    set hw_processor [common::get_property HW_INSTANCE $proc_instance]

    set proc_type [common::get_property IP_NAME [hsi::get_cells -hier $hw_processor]];
}

#-------
# generate: called after OS and library files are copied into project dir
# 	we need to generate the following:
#		1. Makefile options
#		2. System Arch settings for Metal to use
#-------
proc generate {libhandle} {
	# Get the processor
	set proc_instance [hsi::get_sw_processor]
	set hw_processor [common::get_property HW_INSTANCE $proc_instance]
	set proc_type [common::get_property IP_NAME [hsi::get_cells -hier $hw_processor]];
	set os [common::get_property NAME [hsi::get_os]]
	set compiler_str [common::get_property CONFIG.compiler -object ${proc_instance}]
	set compiler_l [split ${compiler_str}]
	set compiler [lindex ${compiler_l} 0]
	set crosscompile [string map {gcc ""} "${compiler}"]
	set c_flags [common::get_property CONFIG.compiler_flags -object ${proc_instance}]
	set extra_flags [common::get_property CONFIG.extra_compiler_flags -object ${proc_instance}]
	set linclude [file normalize "../.."]
	set extra_flags "${extra_flags} -I${linclude}/include"

	# Generate cmake toolchain file
	set toolchain_cmake "toolchain"
	set fd [open "src/libmetal/cmake/platforms/${toolchain_cmake}.cmake" w]

	if { "${proc_type}" == "psu_cortexr5" || "${proc_type}" == "psv_cortexr5"} {
		puts $fd "set (CMAKE_SYSTEM_PROCESSOR \"arm\" CACHE STRING \"\")"
		puts $fd "set (MACHINE \"zynqmp_r5\")"
	} elseif { "${proc_type}" == "psu_cortexa53" } {
		puts $fd "set (CMAKE_SYSTEM_PROCESSOR \"arm\" CACHE STRING \"\")"
		puts $fd "set (MACHINE \"zynqmp_a53\")"
	} elseif { "${proc_type}" == "psv_cortexa72" } {
		puts $fd "set (CMAKE_SYSTEM_PROCESSOR \"arm\" CACHE STRING \"\")"
		puts $fd "set (MACHINE \"zynqmp_a72\")"
	} elseif { "${proc_type}" == "ps7_cortexa9" } {
		puts $fd "set (CMAKE_SYSTEM_PROCESSOR \"arm\" CACHE STRING \"\")"
		puts $fd "set (MACHINE \"zynq7\")"
	} elseif { ("${proc_type}" == "microblaze" || "${proc_type}" == "psu_pmu"
		|| "${proc_type}" == "psu_pmc")
		&& [string match "standalone" "${os}"] > 0} {
		puts $fd "set (CMAKE_SYSTEM_PROCESSOR \"microblaze\" CACHE STRING \"\")"
		puts $fd "set (MACHINE \"microblaze_generic\")"
		set c_flags "${c_flags}  -mlittle-endian"
	}
	puts $fd "set (CROSS_PREFIX \"${crosscompile}\" CACHE STRING \"\")"
	puts $fd "set (CMAKE_C_FLAGS \"${c_flags} ${extra_flags}\" CACHE STRING \"\")"
	if { [string match "freertos*" "${os}"] > 0 } {
		puts $fd "set (CMAKE_SYSTEM_NAME \"FreeRTOS\" CACHE STRING \"\")"
	} else {
		puts $fd "set (CMAKE_SYSTEM_NAME \"Generic\" CACHE STRING \"\")"
	}
	puts $fd "include (CMakeForceCompiler)"
	puts $fd "CMAKE_FORCE_C_COMPILER (\"\$\{CROSS_PREFIX\}gcc\" GNU)"
	puts $fd "CMAKE_FORCE_CXX_COMPILER (\"\$\{CROSS_PREFIX\}g++\" GNU)"

	puts $fd "set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE STRING \"\")"
	puts $fd "set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER CACHE STRING \"\")"
	puts $fd "set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER CACHE STRING \"\")"
	close $fd

	# Run cmake to generate make file
	set bdir "build_libmetal"
	if { [catch {file mkdir "${bdir}"} msg] } {
		error "Failed to create Metal build directory."
	}
	set workdir [pwd]
	cd "${bdir}"
	set cmake_cmd "../src/run_cmake"
	set os_platform_type "$::tcl_platform(platform)"

	set cmake_opt "-DCMAKE_TOOLCHAIN_FILE=toolchain"
	append cmake_opt " -DCMAKE_INSTALL_PREFIX=/"
	append cmake_opt " -DCMAKE_VERBOSE_MAKEFILE=on"
	append cmake_opt " -DWITH_DEFAULT_LOGGER=off"
	append cmake_opt " -DWITH_DOC=off"

	if { [string match -nocase "windows*" "${os_platform_type}"] == 0 } {
		# Linux
		file attributes ${cmake_cmd} -permissions ugo+rx

		if { [catch {exec ${cmake_cmd} "../src/libmetal" ${cmake_opt}} msg] } {
			puts "${msg}"
			error "Failed to generate cmake files."
		} else {
			puts "${msg}"
		}

	} else {
		# Windows
		#
		# Note: windows tcl exec does not do well when trying to provide ${cmake_opt}
		#       for now hardcoding the values directly on the command line.
		if { [catch {exec ${cmake_cmd} "../src/libmetal" -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=toolchain -DCMAKE_INSTALL_PREFIX=/ -DCMAKE_VERBOSE_MAKEFILE=on -DWITH_DEFAULT_LOGGER=off -DWITH_DOC=off } msg] } {
			puts "${msg}"
			error "Failed to generate cmake files."
		} else {
			puts "${msg}"
		}
	}

}

#-------
# post_generate: called after generate called on all libraries
#-------
proc post_generate {libhandle} {

}

#-------
# execs_generate: called after BSP's, libraries and drivers have been compiled
#	This procedure builds the libmetal.a library
#-------
proc execs_generate {libhandle} {

}
