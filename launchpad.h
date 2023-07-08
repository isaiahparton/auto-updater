#include <git2.h>
#include <git2/annotated_commit.h>
#include <git2/checkout.h>
#include <git2/merge.h>
#include <git2/remote.h>
#include <git2/types.h>

#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

inline int progress_cb(const char *str, int len, void *data)
{
	(void)data;
	printf("remote: %.*s", len, str);
	fflush(stdout); /* We don't have the \n to force the flush */
	return 0;
}

inline int update_cb(const char *refname, const git_oid *a, const git_oid *b, void *data)
{
	char a_str[GIT_OID_SHA1_HEXSIZE+1], b_str[GIT_OID_SHA1_HEXSIZE+1];
	(void)data;

	git_oid_fmt(b_str, b);
	b_str[GIT_OID_SHA1_HEXSIZE] = '\0';

	if (git_oid_is_zero(a)) {
		printf("[new]     %.20s %s\n", b_str, refname);
	} else {
		git_oid_fmt(a_str, a);
		a_str[GIT_OID_SHA1_HEXSIZE] = '\0';
		printf("[updated] %.10s..%.10s %s\n", a_str, b_str, refname);
	}

	return 0;
}

inline int transfer_progress_cb(const git_indexer_progress *stats, void *payload)
{
	(void)payload;

	if (stats->received_objects == stats->total_objects) {
		printf("Resolving deltas %u/%u\r",
		       stats->indexed_deltas, stats->total_deltas);
	} else if (stats->total_objects > 0) {
		printf("Received %u/%u objects (%u) in %u\" PRIuZ \" bytes\r",
		       stats->received_objects, stats->total_objects,
		       stats->indexed_objects, (unsigned)stats->received_bytes);
	}
	return 0;
}

inline int fetchhead_cb(const char *ref_name, const char *remote_url, const git_oid *oid, unsigned int is_merge, void *payload)
{
    if (is_merge)
    {
        git_oid_cpy((git_oid *)payload, oid);
    }
    return 0;
}

inline void handle_git_error(int error)
{
    const git_error *e = git_error_last();
    printf("Error %d/%d: %s\n", error, e->klass, e->message);
}

inline const char* copy_string(const char* str)
{
    size_t len = strlen(str);
    char* buf = (char*)malloc(len);
    memcpy(buf, str, len);
    return buf;
}

// Just clones or fetches and hard-resets the repo
inline int update_from_repo(const char* remote_url, const char* target_path)
{
    // Perform git operations
    git_libgit2_init();

    DIR* dir = opendir(target_path);
    if (dir) {
        closedir(dir);

        // Open the local repository
        git_repository *repo = NULL;
        {
            int error = git_repository_open(&repo, target_path);
            if (error < 0) {
                printf("Directory is not a repository!\n");
                return error;
            }
        }

        // Create an anonymous remote
        git_remote *remote = NULL;
        {
            int error = git_remote_create_anonymous(&remote, repo, remote_url);
            if (error < 0) {
                handle_git_error(error);
                git_repository_state_cleanup(repo);
                return error;
            }
        }

        // Fetch and create annotated commit
        {
            printf("Checking for updates\n");
            git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
            fetch_opts.callbacks.update_tips = &update_cb;
            fetch_opts.callbacks.sideband_progress = &progress_cb;
            fetch_opts.callbacks.transfer_progress = transfer_progress_cb;

            int error = git_remote_fetch(remote, NULL, &fetch_opts, NULL);
            if (error < 0) {
                handle_git_error(error);
                return error;
            }
        }
        git_annotated_commit *commit = NULL;
        git_oid oid, tree_oid;
        git_repository_fetchhead_foreach(repo, fetchhead_cb, &oid);
        {
            int error = git_annotated_commit_from_fetchhead(&commit, repo, "main", remote_url, &oid);
            if (error < 0) {
                handle_git_error(error);
                return error;
            }
        }
        const git_annotated_commit *heads[1] = {commit};
        git_merge_analysis_t analysis;
        git_merge_preference_t preference;
        {
            int error = git_merge_analysis(&analysis, &preference, repo, heads, 1);
            if (error < 0) {
                handle_git_error(error);
                return error;
            }
        }

        // Check if update is needed
        if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
            printf("Already up to date\n");
        } else {
            // Now merge
            printf("Applying update\n");
            git_merge_options merge_options = GIT_MERGE_OPTIONS_INIT;
            git_checkout_options checkout_options = GIT_CHECKOUT_OPTIONS_INIT;
            {
                int error = git_merge(repo, heads, 1, &merge_options, &checkout_options);
                if (error < 0) {
                    handle_git_error(error);
                    return error;
                }
            }

            // A simple hard reset will work
            {
                int error = git_reset_from_annotated(repo, heads[0], GIT_RESET_HARD, NULL);
                if (error < 0) {
                    handle_git_error(error);
                    return error;
                }
            }
        }

        // Free resources
        git_annotated_commit_free(commit);

        // Clean up repository
        git_repository_state_cleanup(repo);
    } else {
        // First time launching, so download the application
        printf("Downloading app\n");

        // Simple git clone
        git_repository *repo = NULL;
        int error = git_clone(&repo, remote_url, target_path, NULL);
        if (error < 0) {
            handle_git_error(error);
            exit(error);
        }
    }

    // Shut down libgit2
    git_libgit2_shutdown();

    return 0;
}
