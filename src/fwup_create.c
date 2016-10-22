/*
 * Copyright 2014 LKC Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fwup_create.h"
#include "cfgfile.h"
#include "util.h"
#include "fwfile.h"
#include "../config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>

static int compute_file_metadata(cfg_t *cfg)
{
    cfg_t *sec;
    int i = 0;

    while ((sec = cfg_getnsec(cfg, "file-resource", i++)) != NULL) {
        const char *paths = cfg_getstr(sec, "host-path");
        if (!paths)
            ERR_RETURN("host-path must be set for file-resource '%s'", cfg_title(sec));

        struct fwup_hash_state hash_state;
        fwup_hash_init(&hash_state);

        size_t total = 0;
        char *paths_copy = strdup(paths);
        for (char *path = strtok(paths_copy, ";");
             path != NULL;
             path = strtok(NULL, ";")) {
            FILE *fp = fopen(path, "rb");
            if (!fp) {
                set_last_error("can't open path '%s' in file-resource '%s'", path, cfg_title(sec));
                free(paths_copy);
                return -1;
            }

            char buffer[1024];
            size_t len = fread(buffer, 1, sizeof(buffer), fp);
            while (len > 0) {
                fwup_hash_update(&hash_state, (unsigned char*) buffer, len);
                total += len;
                len = fread(buffer, 1, sizeof(buffer), fp);
            }
            fclose(fp);
        }
        free(paths_copy);

        fwup_hash_final(&hash_state);

#ifndef USE_TWEETNACL
        cfg_setstr(sec, "blake2b-256", hash_state.blake2b_out_str);
#endif
        cfg_setstr(sec, "sha256", hash_state.sha256_out_str);

        cfg_setint(sec, "length", total);
    }

    return 0;
}

static int add_file_resources(cfg_t *cfg, struct archive *a)
{
    cfg_t *sec;
    int i = 0;

    while ((sec = cfg_getnsec(cfg, "file-resource", i++)) != NULL) {
        const char *hostpath = cfg_getstr(sec, "host-path");
        if (!hostpath)
            ERR_RETURN("specify a host-path");

        struct fwfile_assertions assertions;
        assertions.assert_lte = cfg_getint(sec, "assert-size-lte") * 512;
        assertions.assert_gte = cfg_getint(sec, "assert-size-gte") * 512;

        OK_OR_RETURN(fwfile_add_local_file(a, cfg_title(sec), hostpath, &assertions));
    }

    return 0;
}

static int create_archive(cfg_t *cfg, const char *filename, const unsigned char *signing_key)
{
    int rc = 0;
    struct archive *a = archive_write_new();
    if (archive_write_set_format_zip(a) != ARCHIVE_OK ||
        archive_write_zip_set_compression_deflate(a) != ARCHIVE_OK)
        ERR_CLEANUP_MSG("error configuring libarchive: %s", archive_error_string(a));

    // Setting the compression-level is only supported on more recent versions
    // of libarchive, so don't check for errors.
    archive_write_set_format_option(a, "zip", "compression-level", "9");

    if (archive_write_open_filename(a, filename) != ARCHIVE_OK)
        ERR_CLEANUP_MSG("error creating archive '%s': %s", filename, archive_error_string(a));

    OK_OR_CLEANUP(fwfile_add_meta_conf(cfg, a, signing_key));

    OK_OR_CLEANUP(add_file_resources(cfg, a));

cleanup:
    archive_write_close(a);
    archive_write_free(a);

    return rc;
}

int fwup_create(const char *configfile, const char *output_firmware, const unsigned char *signing_key)
{
    cfg_t *cfg = NULL;
    int rc = 0;

    // Parse configuration
    OK_OR_CLEANUP(cfgfile_parse_file(configfile, &cfg));

    // Automatically add fwup metadata
    cfg_setstr(cfg, "meta-creation-date", get_creation_timestamp());
    cfg_setstr(cfg, "meta-fwup-version", PACKAGE_VERSION);

    // Compute all metadata
    OK_OR_CLEANUP(compute_file_metadata(cfg));

    // Create the archive
    OK_OR_CLEANUP(create_archive(cfg, output_firmware, signing_key));

cleanup:
    if (cfg)
        cfgfile_free(cfg);

    return rc;
}
