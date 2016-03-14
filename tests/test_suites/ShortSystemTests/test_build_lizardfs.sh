timeout_set 5 minutes

CHUNKSERVERS=8 \
	USE_RAMDISK=YES \
	MOUNT_EXTRA_CONFIG="mfscachemode=NEVER" \
	CHUNKSERVER_EXTRA_CONFIG="READ_AHEAD_KB = 1024|MAX_READ_BEHIND_KB = 2048"
	setup_local_empty_lizardfs info

cd ${info[mount0]}

mkdir work
mfssetgoal ec43 work

cd work

git clone "https://github.com/lizardfs/lizardfs.git"

cd lizardfs

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j4
