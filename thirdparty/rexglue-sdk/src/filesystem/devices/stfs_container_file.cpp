/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/filesystem/devices/stfs_container_entry.h>
#include <rex/filesystem/devices/stfs_container_file.h>

#include <algorithm>
#include <cmath>

#include <rex/math.h>

namespace rex::filesystem {

StfsContainerFile::StfsContainerFile(uint32_t file_access, StfsContainerEntry* entry)
    : File(file_access, entry), entry_(entry) {}

StfsContainerFile::~StfsContainerFile() = default;

void StfsContainerFile::Destroy() {
  delete this;
}

X_STATUS StfsContainerFile::ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                                     size_t* out_bytes_read) {
  if (byte_offset >= entry_->size()) {
    return X_STATUS_END_OF_FILE;
  }

  size_t src_offset = 0;
  uint8_t* p = buffer.data();
  size_t remaining_length = std::min(buffer.size(), entry_->size() - byte_offset);

  // [ps5 port] Blocks that follow each other in the package (most of a file: STFS puts a hash
  // block after every 170) are read with one fread, not one seek and read per 4 KB block: on the
  // PS5 those small reads ran at 4 MB/s, a 3 GB package took 12 minutes to install.
  FILE* run_file = nullptr;
  size_t run_offset = 0;
  size_t run_length = 0;
  auto read_run = [&]() {
    if (!run_length) {
      return true;
    }
    rex::filesystem::Seek(run_file, run_offset, SEEK_SET);
    auto num_read = fread(p, 1, run_length, run_file);
    *out_bytes_read += num_read;
    p += num_read;
    const bool complete = num_read == run_length;
    run_length = 0;
    return complete;
  };

  *out_bytes_read = 0;
  for (size_t i = 0; i < entry_->block_list().size(); i++) {
    auto& record = entry_->block_list()[i];
    if (src_offset + record.length <= byte_offset) {
      // Doesn't begin in this region. Skip it.
      src_offset += record.length;
      continue;
    }

    size_t read_offset = (byte_offset > src_offset) ? byte_offset - src_offset : 0;
    size_t read_length = std::min(record.length - read_offset, remaining_length);

    FILE* file = entry_->files()->at(record.file);
    const size_t offset = record.offset + read_offset;
    if (run_length && (file != run_file || offset != run_offset + run_length)) {
      if (!read_run()) {
        return X_STATUS_SUCCESS;
      }
    }
    if (!run_length) {
      run_file = file;
      run_offset = offset;
    }
    run_length += read_length;

    src_offset += record.length;
    remaining_length -= read_length;
    if (remaining_length == 0) {
      break;
    }
  }
  read_run();

  return X_STATUS_SUCCESS;
}

}  // namespace rex::filesystem
