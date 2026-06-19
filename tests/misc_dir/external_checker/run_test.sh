# Build the reject-all external checker as a shared library and confirm that
# loading it via `--external-checker-lib` makes every declaration fail kernel
# checking with the mock's error message.
leanc ${LEANC_OPTS-} -O3 -DNDEBUG -shared -o reject_all.so reject_all.c

capture_only ExtCheck.lean \
  lean --external-checker-lib=reject_all.so ExtCheck.lean
check_out_file
check_exit_is_fail
