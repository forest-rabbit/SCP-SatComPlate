/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SHA256_H
#define SATCOMPUTE_SHA256_H

#include <filesystem>
#include <string>
#include <string_view>

namespace ns3
{

/**
 * Compute the lowercase SHA-256 digest of an in-memory byte sequence.
 *
 * @param bytes Input bytes.
 * @return 64-character lowercase hexadecimal digest.
 */
std::string Sha256Bytes(std::string_view bytes);

/**
 * Compute the lowercase SHA-256 digest of a file without loading it all at once.
 *
 * @param path Input file path.
 * @return 64-character lowercase hexadecimal digest.
 * @throws std::runtime_error when the file cannot be read.
 */
std::string Sha256File(const std::filesystem::path& path);

} // namespace ns3

#endif // SATCOMPUTE_SHA256_H
