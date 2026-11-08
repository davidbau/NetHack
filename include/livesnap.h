/* NetHack 5.0 - livesnap.h */
#ifndef LIVESNAP_H
#define LIVESNAP_H

#include "hack.h"
#include "yyjson.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Serializes the current live game state into a Livesnap II JSON string.
 * Caller must free() the returned string. Returns NULL on failure. */
char *serialize_livesnap_bundle(void);

/* Saves the current live game state into a Livesnap II JSON file.
 * Returns 1 on success, 0 on failure. */
int save_livesnap_to_file(const char *path);

/* Restores game state from a Livesnap II JSON string.
 * Returns 1 on success, 0 on failure. */
int restore_livesnap_from_string(const char *json_str, size_t len);

/* Restores game state from a Livesnap II JSON file.
 * Returns 1 on success, 0 on failure. */
int restore_livesnap_from_file(const char *path);

/* Adjudication entry point:
 * Restores snap_file, executes key_sequence (if non-null),
 * and optionally dumps the final state to out_snap_file.
 * Returns 0 on success, non-zero on error. */
int run_livesnap_adjudication(const char *snap_file, const char *key_sequence, const char *out_snap_file);

extern const char *livesnap_adj_snap;
extern const char *livesnap_adj_keys;
extern const char *livesnap_adj_out;

#ifdef __cplusplus
}
#endif

#endif /* LIVESNAP_H */
