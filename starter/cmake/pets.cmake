# Pets (docs/pets.md). add_pets(<target> SOURCE <dir> OUTPUT <dir> [INSTALL] [BUNDLED <id>…]) builds every pet
# folder under SOURCE (a folder with a pet.json) into OUTPUT, mapping SOURCE to the resource prefix assets/:
#   artwork-<sha256 of a frame folder's resource path>.rcc  one pack per frame folder
#   pets/<id>/packs.json      the pet's pack list and hash tree (cmake/pet_tree.cmake)
#   artwork.rcc               the index: each pet's pet.json, animations.json, preview and packs.json
#   artwork-packs.json        the bundled packs' file names, for scripts/package_macos.py and package_windows.py
# BUNDLED names the pets whose packs ship (every pet when omitted); the index lists every pet, and the others'
# packs download on demand (scripts/pet_blobs.py prepares them for the `pets` release).
# Resource files use format 1 without compression, as qt_add_binary_resources would, so identical frames
# build identical packs that updates reuse. Every command belongs to <target>, so the tree step can depend
# on the packs it hashes.
set(PET_TREE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/pet_tree.cmake")

# Writes a configure-time file only when its content changes, so what depends on it rebuilds only then.
function(pet_write path content)
    if(EXISTS "${path}")
        file(READ "${path}" existing)
        if(existing STREQUAL content)
            return()
        endif()
    endif()
    file(WRITE "${path}" "${content}")
endfunction()

# The rcc command qt_add_binary_resources runs, so VPet's packs keep their bytes.
function(pet_rcc output name qrc)
    set(options --format-version 1 --no-compress)
    if(NOT QT_FEATURE_zstd)
        list(APPEND options --no-zstd)
    endif()
    add_custom_command(OUTPUT "${output}"
        COMMAND Qt6::rcc ${options} --binary --name ${name} --output "${output}" "${qrc}"
        DEPENDS Qt6::rcc "${qrc}" ${ARGN}
        VERBATIM)
endfunction()

function(add_pets target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "INSTALL" "SOURCE;OUTPUT" "BUNDLED")
    file(GLOB pet_files CONFIGURE_DEPENDS "${arg_SOURCE}/*/pet.json")
    list(SORT pet_files)
    foreach(id IN LISTS arg_BUNDLED)
        if(NOT EXISTS "${arg_SOURCE}/${id}/pet.json")
            message(FATAL_ERROR "Bundled pet ${id} has no ${arg_SOURCE}/${id}/pet.json")
        endif()
    endforeach()
    set(pack_files)
    set(bundled_files)
    set(pack_names)
    set(index_depends)
    set(index_qrc "<RCC><qresource prefix=\"/\">\n")
    foreach(pet_file IN LISTS pet_files)
        get_filename_component(pet_dir "${pet_file}" DIRECTORY)
        get_filename_component(id "${pet_dir}" NAME)
        file(READ "${pet_file}" pet_json)
        string(JSON preview ERROR_VARIABLE preview_error GET "${pet_json}" preview)
        if(preview_error OR NOT preview MATCHES "^[A-Za-z0-9_-][A-Za-z0-9._-]*\\.png$" OR NOT EXISTS "${pet_dir}/${preview}")
            message(FATAL_ERROR "${pet_file}: \"preview\" must name a PNG file in the pet folder")
        endif()
        # Frames live in subfolders, one sequence pack each; the folder's root holds metadata and the preview.
        file(GLOB_RECURSE frames CONFIGURE_DEPENDS RELATIVE "${pet_dir}" "${pet_dir}/*.png")
        list(SORT frames)
        set(sequences)
        foreach(frame IN LISTS frames)
            get_filename_component(sequence "${frame}" DIRECTORY)
            if(sequence STREQUAL "")
                continue()
            endif()
            string(SHA256 pack "assets/${id}/${sequence}")
            list(APPEND sequences "${pack}")
            set(sequence_${pack} "${sequence}")
            list(APPEND frames_${pack} "${frame}")
        endforeach()
        list(REMOVE_DUPLICATES sequences)
        list(SORT sequences)
        set(listing "")
        set(pet_packs)
        foreach(pack IN LISTS sequences)
            set(name "artwork-${pack}")
            set(qrc "<RCC><qresource prefix=\"/\">\n")
            set(sources)
            foreach(frame IN LISTS frames_${pack})
                string(APPEND qrc "<file alias=\"assets/${id}/${frame}\">${pet_dir}/${frame}</file>\n")
                list(APPEND sources "${pet_dir}/${frame}")
            endforeach()
            string(APPEND qrc "</qresource></RCC>\n")
            pet_write("${arg_OUTPUT}/${name}.qrc" "${qrc}")
            pet_rcc("${arg_OUTPUT}/${name}.rcc" ${target}_${pack} "${arg_OUTPUT}/${name}.qrc" ${sources})
            list(APPEND pet_packs "${arg_OUTPUT}/${name}.rcc")
            if(NOT arg_BUNDLED OR id IN_LIST arg_BUNDLED)
                list(APPEND pack_names "\"${name}.rcc\"")
                list(APPEND bundled_files "${arg_OUTPUT}/${name}.rcc")
            endif()
            string(APPEND listing "${name} ${sequence_${pack}}\n")
        endforeach()
        list(APPEND pack_files ${pet_packs})
        set(tree "${arg_OUTPUT}/pets/${id}/packs.json")
        pet_write("${arg_OUTPUT}/pets/${id}/packs.txt" "${listing}")
        add_custom_command(OUTPUT "${tree}"
            COMMAND "${CMAKE_COMMAND}" -DPET=${pet_dir} -DPREVIEW=${preview} -DLIST=${arg_OUTPUT}/pets/${id}/packs.txt
                -DPACKS=${arg_OUTPUT} -DOUT=${tree} -P "${PET_TREE_SCRIPT}"
            DEPENDS "${PET_TREE_SCRIPT}" "${arg_OUTPUT}/pets/${id}/packs.txt" "${pet_file}" "${pet_dir}/animations.json"
                "${pet_dir}/${preview}" ${pet_packs}
            VERBATIM)
        foreach(file IN ITEMS pet.json animations.json ${preview})
            string(APPEND index_qrc "<file alias=\"assets/${id}/${file}\">${pet_dir}/${file}</file>\n")
        endforeach()
        string(APPEND index_qrc "<file alias=\"assets/${id}/packs.json\">${tree}</file>\n")
        list(APPEND index_depends "${pet_file}" "${pet_dir}/animations.json" "${pet_dir}/${preview}" "${tree}")
    endforeach()
    string(APPEND index_qrc "</qresource></RCC>\n")
    pet_write("${arg_OUTPUT}/artwork.qrc" "${index_qrc}")
    pet_rcc("${arg_OUTPUT}/artwork.rcc" ${target}_catalog "${arg_OUTPUT}/artwork.qrc" ${index_depends})
    list(SORT pack_names)
    list(JOIN pack_names "," joined)
    pet_write("${arg_OUTPUT}/artwork-packs.json" "[${joined}]\n")
    add_custom_target(${target} ALL DEPENDS "${arg_OUTPUT}/artwork.rcc" ${pack_files})
    if(arg_INSTALL)
        install(FILES "${arg_OUTPUT}/artwork.rcc" ${bundled_files} DESTINATION ${CMAKE_INSTALL_DATADIR}/agent-pet)
    endif()
endfunction()
