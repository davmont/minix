#!/bin/sh
#
# Resync files in the tree from a NetBSD branch.
#
# Usage:
#   NETBSD_BRANCH=netbsd/netbsd-10 ./releasetools/netbsd-resync.sh [path]
#
# When [path] is given (e.g. "external/bsd/file"), only files under that
# path are considered.  When omitted, the whole tree minus ./minix/ is
# considered (the original behaviour).
#
# Files that contain the string "minix" anywhere in their content are
# treated as MINIX-modified and SKIPPED — only "pristine NetBSD" files
# are checked out from ${NETBSD_BRANCH}.  This preserves all our local
# patches, including those guarded with "#if defined(__MINIX)" blocks.
#
# Files in the target path that exist locally but not in ${NETBSD_BRANCH}
# are left alone (no deletions).  Files added in ${NETBSD_BRANCH} that
# don't exist locally are also not pulled — add them with an explicit
# `git checkout ${NETBSD_BRANCH} -- <file>` if needed.

: ${BUILDSH=build.sh}

if [ ! -f ${BUILDSH} ]
then
	echo "Please invoke me from the root source dir, where ${BUILDSH} is."
	exit 1
fi

if [ -z "${NETBSD_BRANCH}" ]
then
	echo "NETBSD_BRANCH is undefined.  Example:" >&2
	echo "  NETBSD_BRANCH=netbsd/netbsd-10 $0 external/bsd/file" >&2
	exit 1
fi

if ! git rev-parse --verify "${NETBSD_BRANCH}" >/dev/null 2>&1
then
	echo "NETBSD_BRANCH=${NETBSD_BRANCH} does not resolve to a git ref." >&2
	echo "Did you forget:" >&2
	echo "  git remote add netbsd https://github.com/NetBSD/src.git" >&2
	echo "  git fetch --filter=blob:none --no-tags netbsd netbsd-10" >&2
	exit 1
fi

SCOPE="${1:-.}"
echo "Resync from ${NETBSD_BRANCH}, scope: ${SCOPE}" >&2

# Local files in scope, excluding the ./minix/ subtree and .git
find "${SCOPE}" -type f                    \
    | sed 's|^\./||'                       \
    | grep -v '^minix/'                    \
    | grep -v '^\.git/'                    \
    | sort -u > files.all

# Files whose content mentions "minix" (case-insensitive) — treat as
# MINIX-modified and skip.  Restrict the grep to the same scope.
if [ "${SCOPE}" = "." ]; then
    git grep -l -i minix \
        | grep -v '^minix/'                \
        | sort -u > files.minix
else
    git grep -l -i minix -- "${SCOPE}"     \
        | grep -v '^minix/'                \
        | sort -u > files.minix || :
fi

# files.netbsd = files.all − files.minix
comm -23 files.all files.minix > files.netbsd

n_resync=$(wc -l < files.netbsd | tr -d ' ')
n_skip=$(wc -l < files.minix | tr -d ' ')
echo "Will resync ${n_resync} files (skipping ${n_skip} MINIX-modified)" >&2

while IFS= read -r file
do
    # Only check out if the file exists in the NetBSD branch — otherwise
    # leave the local copy alone (could be MINIX-specific or already gone
    # upstream).
    if git rev-parse --verify "${NETBSD_BRANCH}:${file}" >/dev/null 2>&1
    then
        git checkout "${NETBSD_BRANCH}" -- "${file}"
    fi
done < files.netbsd

# Second pass: pick up files that exist in the NetBSD branch under
# SCOPE but not locally.  These are typically new source files added
# in newer upstream releases (e.g. file 5.22 → 5.43 added buffer.c,
# der.c, dprintf.c, etc.).
#
# We assume any NEW upstream file is wanted as-is — there's nothing
# MINIX-modified to preserve since it didn't exist locally.
if [ "${SCOPE}" != "." ]; then
    git ls-tree -r --name-only "${NETBSD_BRANCH}" -- "${SCOPE}"  \
        | sort -u > files.upstream
    comm -23 files.upstream files.all > files.new
    n_new=$(wc -l < files.new | tr -d ' ')
    echo "Adding ${n_new} files new in upstream under ${SCOPE}" >&2
    while IFS= read -r file
    do
        [ -n "$file" ] && git checkout "${NETBSD_BRANCH}" -- "${file}"
    done < files.new
fi

echo "Done.  Review with: git diff --stat -- ${SCOPE}" >&2
echo "Sync metadata: files.{all,minix,netbsd${SCOPE:+,upstream,new}}" >&2
