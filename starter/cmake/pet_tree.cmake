# Writes one pet's packs.json (docs/pets.md): the digest of its catalog, one leaf per built sequence pack,
# and the root over both. add_pets (cmake/pets.cmake) runs it after the pet's packs build:
#   cmake -DPET=<pet folder> -DPREVIEW=<preview file name> -DLIST=<packs.txt> -DPACKS=<pack folder>
#         -DOUT=<packs.json> -P pet_tree.cmake
# packs.txt holds one "<pack name> <frame folder>" line per pack, sorted by name.
file(SHA256 "${PET}/pet.json" pet_digest)
file(SHA256 "${PET}/animations.json" animations_digest)
file(SHA256 "${PET}/${PREVIEW}" preview_digest)
string(SHA256 catalog "pet.json ${pet_digest}\nanimations.json ${animations_digest}\n${PREVIEW} ${preview_digest}\n")
set(tree "agent-pet-pet-tree 1\ncatalog ${catalog}\n")
set(entries)
file(STRINGS "${LIST}" lines ENCODING UTF-8)
foreach(line IN LISTS lines)
    string(FIND "${line}" " " space)
    string(SUBSTRING "${line}" 0 ${space} name)
    math(EXPR start "${space} + 1")
    string(SUBSTRING "${line}" ${start} -1 sequence)
    file(SHA256 "${PACKS}/${name}.rcc" digest)
    file(SIZE "${PACKS}/${name}.rcc" bytes)
    string(APPEND tree "${name} ${digest}\n")
    list(APPEND entries "    {\"name\": \"${name}\", \"sequence\": \"${sequence}\", \"sha256\": \"${digest}\", \"bytes\": ${bytes}}")
endforeach()
string(SHA256 root "${tree}")
list(JOIN entries ",\n" packs)
file(WRITE "${OUT}" "{\n  \"root\": \"sha256:${root}\",\n  \"catalog\": \"sha256:${catalog}\",\n  \"packs\": [\n${packs}\n  ]\n}\n")
