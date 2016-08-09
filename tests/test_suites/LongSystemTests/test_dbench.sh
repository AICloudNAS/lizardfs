timeout_set 1 hour
assert_program_installed dbench

# Runs dbench for half an hour with 5 clients
dbench_tester() {
	local dir="$1"
	cd "$dir"
	MESSAGE="Testing directory $dir" expect_success dbench -s -S -t 1800 5
}

CHUNKSERVERS=3 \
	MOUNTS=1 \
	MOUNT_EXTRA_CONFIG="mfscachemode=NEVER" \
	setup_local_empty_lizardfs info

cd "${info[mount0]}"
for goal in 1 2 xor2; do
	mkdir "goal_$goal"
	lizardfs setgoal "$goal" "goal_$goal"
	dbench_tester "goal_$goal" &
done
wait
