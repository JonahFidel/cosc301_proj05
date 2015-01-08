#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}

uint16_t check_dirent(struct direntry *dirent, uint8_t *image_buffer, struct bpb33 *bpb, bool *sectors){
    uint16_t follow = 0;

    int i;
    char name[9];
    char extension[4];
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
    return follow;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
    return follow;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
    // dot entry ("." or "..")
    // skip it
        return follow;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
    if (name[i] == ' ') 
        name[i] = '\0';
    else 
        break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
    if (extension[i] == ' ') 
        extension[i] = '\0';
    else 
        break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
    // ignore any long file name extension entries
    //
    // printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
    if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
            file_cluster = getushort(dirent->deStartCluster);
            follow = file_cluster;
            sectors[follow] = true; 
        }
    }
    else {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
        printf("%s.%s\n", name, extension);
        if (check_file_inconsistency(dirent, image_buffer, bpb, sectors)){
            printf("Entries are inconsistent\n");
            fix_file_incosistency(dirent, image_buffer, bpb);
        }
    }
    return follow;
}

void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent*4; i++)
    printf(" ");
}

uint16_t print_dirent(struct direntry *dirent, int indent)
{
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    uint32_t size;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
    return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
    return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
    // dot entry ("." or "..")
    // skip it
        return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
    if (name[i] == ' ') 
        name[i] = '\0';
    else 
        break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
    if (extension[i] == ' ') 
        extension[i] = '\0';
    else 
        break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
    // ignore any long file name extension entries
    //
    // printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
    {
    printf("Volume: %s\n", name);
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
    if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
        print_indent(indent);
            printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
    int ro = (dirent->deAttributes & ATTR_READONLY) == ATTR_READONLY;
    int hidden = (dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN;
    int sys = (dirent->deAttributes & ATTR_SYSTEM) == ATTR_SYSTEM;
    int arch = (dirent->deAttributes & ATTR_ARCHIVE) == ATTR_ARCHIVE;

    size = getulong(dirent->deFileSize);
    print_indent(indent);
    printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n", 
           name, extension, size, getushort(dirent->deStartCluster),
           ro?'r':' ', 
               hidden?'h':' ', 
               sys?'s':' ', 
               arch?'a':' ');
    }
    return followclust;
}

struct direntry *traverse_root(char *searchpath, uint8_t *image_buf, struct bpb33* bpb)
{
    uint16_t cluster = 0;
    struct direntry *rv = NULL;

    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

    char *next_path_component = index(searchpath, '/');
    int root_entry_len = strlen(searchpath);
    if (next_path_component != NULL)
    {
        root_entry_len = next_path_component - searchpath;
        *next_path_component = '\0';
        next_path_component++;
    }

    char buffer[MAXFILENAME];

    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++)
    {
        uint16_t followclust = get_dirent(dirent, buffer);

        if (strncasecmp(searchpath, buffer, strlen(searchpath)) == 0)
        {
            if (!next_path_component)
                rv = dirent;
            else if (is_valid_cluster(followclust, bpb))
                rv = follow_dir(next_path_component, followclust, image_buf, bpb);
        }

        if (rv)
            break;

        dirent++;
    }

    return rv;
}

void write_dirent(struct direntry *dirent, char *filename, 
          uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) 
    {
    if (p2[i] == '/' || p2[i] == '\\') 
    {
        uppername = p2+i+1;
    }
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) 
    {
    uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) 
    {
    fprintf(stderr, "No filename extension given - defaulting to .___\n");
    }
    else 
    {
    *p = '\0';
    p++;
    len = strlen(p);
    if (len > 3) len = 3;
    memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) 
    {
    uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);
}

void get_name(char *fullname, struct direntry *dirent) 
{
    char name[9];
    char extension[4];
    int i;

    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);

    /* names are space padded - remove the padding */
    for (i = 8; i > 0; i--) 
    {
    if (name[i] == ' ') 
        name[i] = '\0';
    else 
        break;
    }

    /* extensions aren't normally space padded - but remove the
       padding anyway if it's there */
    for (i = 3; i > 0; i--) 
    {
    if (extension[i] == ' ') 
        extension[i] = '\0';
    else 
        break;
    }
    fullname[0]='\0';
    strcat(fullname, name);

    /* append the extension if it's not a directory */
    if ((dirent->deAttributes & ATTR_DIRECTORY) == 0) 
    {
    strcat(fullname, ".");
    strcat(fullname, extension);
    }
}

void create_dirent(struct direntry *dirent, char *filename, 
           uint16_t start_cluster, uint32_t size,
           uint8_t *image_buf, struct bpb33* bpb)
{
    while (1) 
    {
    if (dirent->deName[0] == SLOT_EMPTY) 
    {
        /* we found an empty slot at the end of the directory */
        write_dirent(dirent, filename, start_cluster, size);
        dirent++;

        /* make sure the next dirent is set to be empty, just in
           case it wasn't before */
        memset((uint8_t*)dirent, 0, sizeof(struct direntry));
        dirent->deName[0] = SLOT_EMPTY;
        return;
    }

    if (dirent->deName[0] == SLOT_DELETED) 
    {
        /* we found a deleted entry - we can just overwrite it */
        write_dirent(dirent, filename, start_cluster, size);
        return;
    }
    dirent++;
    }
}


void follow_dir(uint16_t cluster, int indent,
        uint8_t *image_buf, struct bpb33* bpb)
{
    while (is_valid_cluster(cluster, bpb))
    {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
    for ( ; i < numDirEntries; i++)
    {
            
            uint16_t followclust = print_dirent(dirent, indent);
            if (followclust)
                follow_dir(followclust, indent+1, image_buf, bpb);
            dirent++;
    }

    cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}

int main(int argc, char** argv) {
    uint8_t *image_buffer;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
	usage(argv[0]);
    }

    image_buffer = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buffer);

    // your code should start here...

    traverse_root(image_buf, bpb);
    // I'm not really sure what to do here, I know I need to somehow incorporate the rest of the functions
    // in order to fix the broken images

    //checkdirent()
    //printdirent()
    //fix_block_inconsistencies 
    //create_dirent()
    //follow_dir()

    unmmap_file(image_buffer, &fd);
    return 0;
}
