# Operating-System-Class-Project---File-System-Checking
developing a program to check the file system consistency


Rules:
--------------------------------------------------------------------------
1. Each inode is either unallocated or one of the valid types (T_FILE, T_DIR, T_DEV). If not, print ERROR: bad inode.
2. For in-use inodes, each address that is used by the inode is valid (points to a valid datablock address within the image). If the direct block is used and is invalid, print ERROR: bad direct address in inode.; if the indirect block is in use and is invalid, print ERROR: bad indirect address in inode.
3. Root directory exists, its inode number is 1, and the parent of the root directory is itself. If not, print ERROR: root directory does not exist.
4. Each directory contains . and .. entries, and the . entry points to the directory itself. If not, print ERROR: directory not properly formatted.
5. For in-use inodes, each address in use is also marked in use in the bitmap. If not, print ERROR: address used by inode but marked free in bitmap.
6. For blocks marked in-use in bitmap, the block should actually be in-use in an inode or indirect block somewhere. If not, print ERROR: bitmap marks block in use but it is not in use.
7. For in-use inodes, each direct address in use is only used once. If not, print ERROR: direct address used more than once.
8. For in-use inodes, each indirect address in use is only used once. If not, print ERROR: indirect address used more than once.
9. For all inodes marked in use, each must be referred to in at least one directory. If not, print ERROR: inode marked use but not found in a directory.
10. For each inode number that is referred to in a valid directory, it is actually marked in use. If not, print ERROR: inode referred to in directory but marked free.
11. Reference counts (number of links) for regular files match the number of times file is referred to in directories (i.e., hard links work correctly). If not, print ERROR: bad reference count for file.
12. No extra links allowed for directories (each directory only appears in one other
directory). If not, print ERROR: directory appears more than once in file system.
