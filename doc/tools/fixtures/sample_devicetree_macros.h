/*
 * Minimal fixture containing a subset of generated devicetree macros.
 * Values are intentionally simple; only macro identifiers are relevant
 * for the documentation grammar check.
 */

#define DT_N_PATH "\/"
#define DT_N_FOREACH_CHILD(fn) fn(DT_N)
#define DT_N_FOREACH_CHILD_SEP(fn, sep) fn(DT_N)
#define DT_N_FOREACH_CHILD_VARGS(fn, ...) fn(DT_N, __VA_ARGS__)
#define DT_N_FOREACH_CHILD_SEP_VARGS(fn, sep, ...) fn(DT_N, sep, __VA_ARGS__)

#define DT_N_S_soc_PATH "\/soc"
#define DT_N_S_soc_FOREACH_CHILD(fn) fn(DT_N_S_soc)
#define DT_N_S_soc_FOREACH_CHILD_SEP(fn, sep) fn(DT_N_S_soc)
#define DT_N_S_soc_FOREACH_CHILD_VARGS(fn, ...) fn(DT_N_S_soc, __VA_ARGS__)
#define DT_N_S_soc_FOREACH_CHILD_SEP_VARGS(fn, sep, ...) fn(DT_N_S_soc, sep, __VA_ARGS__)
