#ifndef TAR_TESTS_ROOTED_LINK_HOST_H
#define TAR_TESTS_ROOTED_LINK_HOST_H

int linkat_beneath(int rootfd, const char *source_path,
    int expected_source_fd, const char *target_path);
int tar_test_path_link(const char *source_path, const char *target_path);

#endif
