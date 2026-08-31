#ifndef CLIMATOLOGY_CHECKSUM_H
#define CLIMATOLOGY_CHECKSUM_H

#include <cstddef>
#include <string>

namespace climatology {

std::string Sha256(const void* data, std::size_t size);
std::string Sha256File(const std::string& path);

}  // namespace climatology

#endif
