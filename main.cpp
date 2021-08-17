#include <iostream>
#include <vector>
#include <string>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#pragma pack(1)
using namespace std;
#define Directory 0xE5
#define Unused -1
#define Reservedcluster -2
#define LastCluster -3

#define Subdirectory 0x01
#define File 0x02

int SECTOR_SIZE = 512;      // Default 512 byte a sector size
int SECTOR_COUNT = 1 << 12; // 4096 SECTOR
int FAT_SIZE = 4096 - 32;

int fd;
//36 byte
//512 byte 510 0x55 511 0xAA
struct BootSec
{
    int BS_JmpBoot : 3 * 8;
    char BS_OEMName[8];
    int BPB_BytsPerSec : 2 * 8;
    int BPB_SecPerClus : 1 * 8;
    int BPB_RsvdSecCnt : 2 * 8;
    int BPB_NumFATs : 1 * 8;
    int BPB_RootEntCnt : 2 * 8;
    int BPB_TotSec16 : 2 * 8;
    int BPB_Media : 1 * 8;
    int BPB_FATSz16 : 2 * 8;
    int BPB_SecPerTrk : 2 * 8;
    int BPB_NumHeads : 2 * 8;
    int BPB_HiddSec : 4 * 8;
    int BPB_TotSec32 : 4 * 8;
};

struct DirEntry
{
    char Filename[11];
    int Attributes : 1 * 8;
    int Reserved : 2 * 8;
    uint32_t CreationTimeAndDate;
    int LastAccessDate : 2 * 8;
    int Ignore : 2 * 8;
    uint32_t LastWriteTimeAndDate;
    int FirstLogicalCluster : 2 * 8;
    int FileSize : 4 * 8;
};

struct BootSec boot;

vector<string> parse(string str, string delim)
{
    vector<string> tokens;
    char *str_c = strdup(str.c_str());
    char *token = NULL;

    token = strtok(str_c, delim.c_str());
    while (token != NULL)
    {
        tokens.push_back(string(token));
        token = strtok(NULL, delim.c_str());
    }

    delete[] str_c;

    return tokens;
}

vector<string> get_line_parse(string delim)
{
    string str;
    getline(std::cin, str);
    return parse(str, delim);
}

int toPhysAddr(int cluster)
{
    if (cluster < 0 || cluster > 4063)
    {
        if (cluster == 4444)
        {
            return 4444; // Root
        }
        else
        {
            perror("toPhyAdress,cluster out of ..");
        }
    }
    return cluster + 32;
}

void read_fat(int *arr)
{
    lseek(fd, SECTOR_SIZE, SEEK_SET);
    read(fd, arr, FAT_SIZE * sizeof(int));
}

void write_fat(int *arr)
{
    lseek(fd, SECTOR_SIZE, SEEK_SET);
    write(fd, arr, FAT_SIZE * sizeof(int));
}

void read_root(void *buff)
{
    lseek(fd, 16 * SECTOR_SIZE, SEEK_SET);
    read(fd, buff, boot.BPB_RootEntCnt * sizeof(DirEntry));
}

void write_root(void *buff)
{
    lseek(fd, 16 * SECTOR_SIZE, SEEK_SET);
    write(fd, buff, boot.BPB_RootEntCnt * sizeof(DirEntry));
}

int count_unused()
{
    int arr[FAT_SIZE];
    read_fat(arr);

    int count = 0;
    for (int i = 0; i < FAT_SIZE; i++)
    {
        if (arr[i] == -1)
            count++;
    }
    return count;
}

int get_next_unused(bool isLast = false)
{
    int arr[FAT_SIZE];
    read_fat(arr);

    for (int i = 0; i < FAT_SIZE; i++)
    {
        if (arr[i] == -1)
        {
            if (isLast == true)
            {
                arr[i] = LastCluster;
            }
            else
            {
                arr[i] = Reservedcluster;
            }
            write_fat(arr);
            return i;
        }
    }
    return -1;
}

void write_to_cluster(int clusterid, void *buff)
{
    int physec = toPhysAddr(clusterid);
    lseek(fd, physec * SECTOR_SIZE, SEEK_SET);
    write(fd, buff, SECTOR_SIZE);
}

void read_from_cluster(int clusterid, void *buff)
{
    memset(buff, 0, SECTOR_SIZE);
    int physec = toPhysAddr(clusterid);
    lseek(fd, physec * SECTOR_SIZE, SEEK_SET);
    read(fd, buff, SECTOR_SIZE);
}

void create_parent_dir(int clusterid, int parentdir)
{
    //parentdir==-1 -> root
    int len = SECTOR_SIZE / sizeof(DirEntry);
    struct DirEntry dirs[len];
    memset(dirs, 0, len * sizeof(DirEntry));
    strcpy(dirs[0].Filename, ".");
    dirs[0].FirstLogicalCluster = clusterid;
    dirs[0].FileSize = 0;
    dirs[0].Attributes = Subdirectory;
    dirs[0].CreationTimeAndDate = (int32_t)time(NULL);
    dirs[0].LastWriteTimeAndDate = (int32_t)time(NULL);

    strcpy(dirs[1].Filename, "..");
    dirs[1].FirstLogicalCluster = parentdir;
    dirs[1].FileSize = 0;
    dirs[1].Attributes = Subdirectory;
    dirs[1].CreationTimeAndDate = (int32_t)time(NULL);
    dirs[1].LastWriteTimeAndDate = (int32_t)time(NULL);

    write_to_cluster(clusterid, &dirs);
}

int find_dir_size(struct DirEntry *dirs, int arrLen)
{
    int size = 0;
    struct DirEntry empty;
    memset(&empty, 0, sizeof(DirEntry));
    for (int i = 0; i < arrLen; i++)
    {
        int n = memcmp(&dirs[i], &empty, sizeof(DirEntry));
        if (n != 0)
        {
            size++;
        }
        else
        {
            break;
        }
    }
    return size;
}

int find_dir_cluster(struct DirEntry *dirs, int size, string dirname)
{

    for (int i = 0; i < size; i++)
    {

        if (dirs[i].Filename == dirname)
        {
            if (dirs[i].Attributes != Subdirectory)
            {
                return -1;
            }
            return dirs[i].FirstLogicalCluster;
        }
    }
    return -1; // not found
}

int get_last_dir(string path)
{

    vector<string> p = parse(path, "\\\"");

    int len = boot.BPB_RootEntCnt;

    struct DirEntry rootdirs[len];
    read_root(rootdirs);
rootdiretoryprint:
    if (p.size() == 0)
    {
        return -1;
    }
    else if (p.size() == 1)
    {
        return 4444;
    }
    else
    {

        int cid = find_dir_cluster(rootdirs, find_dir_size(rootdirs, len), p[0]);

        if (cid < 0)
        {

            return -1;
        }

        int lenc = SECTOR_SIZE / sizeof(DirEntry);
        struct DirEntry dirs[lenc];
        read_from_cluster(cid, &dirs);

        if (p.size() > 1)
        {

            for (int i = 1; i < p.size() - 1; i++)
            {
                cid = find_dir_cluster(dirs, find_dir_size(dirs, lenc), p[i]);
                if (cid < 0)
                {

                    return -1;
                }
                if (cid == 4444)
                {
                    vector<string> newp = {p.begin() + i + 1, p.end()};
                    p = newp;
                    goto rootdiretoryprint;
                }
                read_from_cluster(cid, &dirs);
            }
        }

        return cid;
    }
}

void mkdir(string path)
{

    vector<string> p = parse(path, "\\\"");
    int len = boot.BPB_RootEntCnt;

    struct DirEntry rootdirs[len];
    read_root(rootdirs);

    if (p.size() == 0)
        return;
    //add root
    if (p.size() == 1)
    {
        int size = find_dir_size(rootdirs, len);

        if (size == len)
        {
            cout << "mkdir: there is no enough space" << endl;
        }

        if (count_unused() >= 1)
        {

            strcpy(rootdirs[size].Filename, p[0].c_str());
            rootdirs[size].Attributes = Subdirectory;
            rootdirs[size].FirstLogicalCluster = get_next_unused(true);
            rootdirs[size].CreationTimeAndDate = (uint32_t)time(NULL);
            rootdirs[size].LastWriteTimeAndDate = (uint32_t)time(NULL);
            rootdirs[size].FileSize = 0;
            //time(&dirs[i].CreationTimeAndDate);
            //time(&dirs[i].LastWriteTimeAndDate);
            write_root(rootdirs);
            create_parent_dir(rootdirs[size].FirstLogicalCluster, 4444);
        }
        else
        {
            cout << "mkdir: there no enough cluster" << endl;
        }
    }
    else if (p.size() > 1)
    {
        int cid = get_last_dir(path);

        int lenc = SECTOR_SIZE / sizeof(DirEntry);
        struct DirEntry dirs[lenc];
        read_from_cluster(cid, &dirs);

        int s = find_dir_size(dirs, lenc);
        if (count_unused() >= 1)
        {

            strcpy(dirs[s].Filename, p[p.size() - 1].c_str());
            dirs[s].FirstLogicalCluster = get_next_unused(true);

            dirs[s].FileSize = 0;
            dirs[s].Attributes = Subdirectory;
            dirs[s].CreationTimeAndDate = (uint32_t)time(NULL);
            dirs[s].LastWriteTimeAndDate = (uint32_t)time(NULL);
            //time(&dirs[i].CreationTimeAndDate);
            //time(&dirs[i].LastWriteTimeAndDate);
            write_to_cluster(cid, dirs);
            create_parent_dir(dirs[s].FirstLogicalCluster, cid);
        }
        else
        {
            cout << "mkdir: there no enough cluster" << endl;
        }
    }
}

void recursivermdir(int clusterid)
{

    int len = SECTOR_SIZE / sizeof(DirEntry);
    struct DirEntry dirs[len];
    memset(dirs, 0, sizeof(DirEntry) * len);
    int size;
    int i = 0;
    if (clusterid == 4444)
    {
        cout << "root can not delete" << endl;
        return;
    }
    else
    {
        i = 2;
        read_from_cluster(clusterid, dirs);
        size = find_dir_size(dirs, len);
    }

    if (size == 2)
    {
        return;
    }

    for (; i < size; i++)
    {
        if (dirs[i].Attributes == File)
        {
            int cid = dirs[i].FirstLogicalCluster;
            int arr[FAT_SIZE];
            read_fat(arr);

            do
            {
                int tmpcid = arr[cid];
                if (arr[cid] == LastCluster)
                {
                    arr[cid] = Unused;
                    break;
                }
                arr[cid] = Unused;

                cid = tmpcid;
            } while (1);

            write_fat(arr);
        }
        else
        {
            recursivermdir(dirs[i].FirstLogicalCluster);
            int cid = dirs[i].FirstLogicalCluster;
            int arr[FAT_SIZE];
            read_fat(arr);
            arr[cid] = Unused;
            write_fat(arr);
        }
    }

    int cid = clusterid;
    int arr[FAT_SIZE];
    read_fat(arr);
    arr[cid] = Unused;
    write_fat(arr);
}

void rmdir(string path)
{
    int parentcid = get_last_dir(path);
    if (parentcid < 0)
    {
        cout << "path not found" << endl;
        return;
    }
    vector<string> p = parse(path, "\\\"");
    string filename = p[p.size() - 1];

    struct DirEntry empty;
    memset(&empty, 0, sizeof(DirEntry));

    if (parentcid == 4444)
    {
        int len = boot.BPB_RootEntCnt;

        struct DirEntry rootdirs[len];
        read_root(rootdirs);

        int entrysize = find_dir_size(rootdirs, len);
        int i = 0;
        for (; i < entrysize; i++)
        {
            if (rootdirs[i].Filename == filename && rootdirs[i].Attributes == Subdirectory)
            {
                recursivermdir(rootdirs[i].FirstLogicalCluster);

                rootdirs[i] = empty;
                break;
            }
        }

        for (; i < entrysize; i++)
        {
            rootdirs[i] = rootdirs[i + 1];
        }

        write_root(rootdirs);
    }
    else
    {

        int lenc = SECTOR_SIZE / sizeof(DirEntry);
        struct DirEntry dirs[lenc];
        read_from_cluster(parentcid, &dirs);

        int entrysize = find_dir_size(dirs, lenc);

        int i = 0;
        for (; i < entrysize; i++)
        {
            if (dirs[i].Filename == filename && dirs[i].Attributes == Subdirectory)
            {
                recursivermdir(dirs[i].FirstLogicalCluster);

                dirs[i] = empty;
                break;
            }
        }

        for (; i < entrysize; i++)
        {
            dirs[i] = dirs[i + 1];
        }

        write_root(dirs);
    }
}

void listdir(struct DirEntry *dirs, int size)
{
    for (int i = 0; i < size; i++)
    {
        if (dirs[i].Attributes == Subdirectory)
        {
            time_t t = (time_t)(dirs[i].LastWriteTimeAndDate);
            char *timeStr = asctime(gmtime(&t));
            timeStr[strlen(timeStr) - 1] = '\0';

            cout << timeStr << " "
                 << "\t<DIR>\t " << dirs[i].Filename << endl;
        }
        else
        {
            time_t t = (time_t)(dirs[i].LastWriteTimeAndDate);
            char *timeStr = asctime(gmtime(&t));
            timeStr[strlen(timeStr) - 1] = '\0';
            cout << timeStr << " "
                 << "\t " << dirs[i].FileSize << " " << dirs[i].Filename << endl;
        }
    }
}

void dir(string path)
{
    vector<string> p = parse(path, "\\\"");

    int len = boot.BPB_RootEntCnt;

    struct DirEntry rootdirs[len];
    read_root(rootdirs);
rootdiretoryprint:
    if (p.size() == 0)
    {
        int size = find_dir_size(rootdirs, len);
        listdir(rootdirs, size);
    }
    else
    {

        int cid = find_dir_cluster(rootdirs, find_dir_size(rootdirs, len), p[0]);

        if (cid < 0)
        {
            cout << p[0] << "directory is not found in root" << endl;
            return;
        }

        int lenc = SECTOR_SIZE / sizeof(DirEntry);
        struct DirEntry dirs[lenc];
        read_from_cluster(cid, &dirs);

        if (p.size() > 1)
        {

            for (int i = 1; i < p.size(); i++)
            {
                cid = find_dir_cluster(dirs, find_dir_size(dirs, lenc), p[i]);
                if (cid < 0)
                {

                    cout << p[0] << " directory is not found in root" << endl;
                    return;
                }
                if (cid == 4444)
                {
                    vector<string> newp = {p.begin() + i + 1, p.end()};
                    p = newp;
                    goto rootdiretoryprint;
                }
                read_from_cluster(cid, &dirs);
            }
        }

        int l = find_dir_size(dirs, lenc);

        listdir(dirs, l);
    }
}

void write_file(string path, string otherfile)
{

    vector<string> p = parse(path, "\\\"");
    struct stat st;
    string filename = p[p.size() - 1];
    stat(otherfile.c_str(), &st);
    int filesize = st.st_size;
    int sectorsize = (filesize / SECTOR_SIZE) + 1;

    int parentcid = get_last_dir(path);
    if (parentcid < 0)
    {
        cout << "path not found" << endl;
        return;
    }

    int fdl = open(otherfile.c_str(), O_RDWR, 0644);
    if (fdl == -1)
    {
        perror("openfile, write_file");
        exit(0);
    }
    int parentEntc;
    int cid;

    if (parentcid == 4444)
    {
        int len = boot.BPB_RootEntCnt;

        struct DirEntry rootdirs[len];
        read_root(rootdirs);

        int entrysize = find_dir_size(rootdirs, len);

        if (entrysize >= len)
        {
            cout << "There is no enough space, write_file" << endl;
            return;
        }

        strcpy(rootdirs[entrysize].Filename, filename.c_str());

        rootdirs[entrysize].FileSize = filesize;
        rootdirs[entrysize].Attributes = File;
        rootdirs[entrysize].CreationTimeAndDate = (uint32_t)time(NULL);
        rootdirs[entrysize].LastWriteTimeAndDate = (uint32_t)time(NULL);
        rootdirs[entrysize].FileSize = st.st_size;
        cid = get_next_unused();
        rootdirs[entrysize].FirstLogicalCluster = cid;
        write_root(rootdirs);
    }
    else
    {

        int lenc = SECTOR_SIZE / sizeof(DirEntry);
        struct DirEntry dirs[lenc];
        read_from_cluster(parentcid, &dirs);

        int entrysize = find_dir_size(dirs, lenc);

        if (entrysize >= lenc)
        {
            cout << "There is no enough space, write_file" << endl;
            return;
        }

        strcpy(dirs[entrysize].Filename, filename.c_str());

        dirs[entrysize].FileSize = filesize;
        dirs[entrysize].Attributes = File;
        dirs[entrysize].CreationTimeAndDate = (uint32_t)time(NULL);
        dirs[entrysize].LastWriteTimeAndDate = (uint32_t)time(NULL);
        dirs[entrysize].FileSize = st.st_size;
        cid = get_next_unused();
        dirs[entrysize].FirstLogicalCluster = cid;

        write_to_cluster(parentcid, dirs);
    }

    if (sectorsize > count_unused())
    {
        cout << "There is no enough space, write_file" << endl;
        return;
    }
    int arr[FAT_SIZE];
    read_fat(arr);

    while (1)
    {
        char buffer[SECTOR_SIZE];
        memset(buffer, 0, SECTOR_SIZE);
        int n = read(fdl, buffer, SECTOR_SIZE);
        write_to_cluster(cid, buffer);
        filesize -= n;
        if (filesize <= 0)
            break;
        arr[cid] = get_next_unused();
        cid = arr[cid];
    }

    arr[cid] = LastCluster;
    write_fat(arr);
    close(fdl);
}

void read_file(string path, string otherfile)
{

    vector<string> p = parse(path, "\\\"");

    string filename = p[p.size() - 1];

    int parentcid = get_last_dir(path);
    if (parentcid < 0)
    {
        cout << "path not found" << endl;
        return;
    }

    int fdl = open(otherfile.c_str(), O_CREAT | O_RDWR, 0644);
    if (fdl == -1)
    {
        perror("openfile, write_file");
        exit(0);
    }

    int parentEntc;
    int cid;
    int filesize = 0;

    if (parentcid == 4444)
    {
        int len = boot.BPB_RootEntCnt;

        struct DirEntry rootdirs[len];
        read_root(rootdirs);

        int entrysize = find_dir_size(rootdirs, len);

        for (int i = 0; i < entrysize; i++)
        {
            if (rootdirs[i].Filename == filename && rootdirs[i].Attributes == File)
            {
                cid = rootdirs[i].FirstLogicalCluster;
                filesize = rootdirs[i].FileSize;
                break;
            }
        }
    }
    else
    {

        int lenc = SECTOR_SIZE / sizeof(DirEntry);
        struct DirEntry dirs[lenc];
        read_from_cluster(parentcid, &dirs);

        int entrysize = find_dir_size(dirs, lenc);

        for (int i = 0; i < entrysize; i++)
        {
            if (dirs[i].Filename == filename && dirs[i].Attributes == File)
            {
                cid = dirs[i].FirstLogicalCluster;
                filesize = dirs[i].FileSize;
                break;
            }
        }
    }

    int arr[FAT_SIZE];
    read_fat(arr);

    do
    {
        char buffer[SECTOR_SIZE];
        read_from_cluster(cid, buffer);
        int n = write(fdl, buffer, filesize < SECTOR_SIZE ? filesize : SECTOR_SIZE);
        filesize -= n;
        if (arr[cid] == LastCluster)
        {
            break;
        }

        cid = arr[cid];
    } while (1);

    close(fdl);
}

void del(string path)
{

    struct DirEntry empty;
    memset(&empty, 0, sizeof(DirEntry));

    int parentcid = get_last_dir(path);
    if (parentcid < 0)
    {
        cout << "path not found" << endl;
        return;
    }
    int cid;
    vector<string> p = parse(path, "\\\"");

    string filename = p[p.size() - 1];
    if (parentcid == 4444)
    {
        int len = boot.BPB_RootEntCnt;

        struct DirEntry rootdirs[len];
        read_root(rootdirs);

        int entrysize = find_dir_size(rootdirs, len);
        int i = 0;
        for (; i < entrysize; i++)
        {
            if (rootdirs[i].Filename == filename)
            {
                cid = rootdirs[i].FirstLogicalCluster;
                rootdirs[i] = empty;

                break;
            }
        }

        for (; i < entrysize; i++)
        {
            rootdirs[i] = rootdirs[i + 1];
        }

        write_root(rootdirs);
    }
    else
    {

        int lenc = SECTOR_SIZE / sizeof(DirEntry);
        struct DirEntry dirs[lenc];
        read_from_cluster(parentcid, &dirs);

        int entrysize = find_dir_size(dirs, lenc);
        int i = 0;
        for (; i < entrysize; i++)
        {
            if (dirs[i].Filename == filename)
            {
                cid = dirs[i].FirstLogicalCluster;
                dirs[i] = empty;

                break;
            }
        }

        for (; i < entrysize; i++)
        {
            dirs[i] = dirs[i + 1];
        }

        write_to_cluster(parentcid, dirs);
    }

    int arr[FAT_SIZE];
    read_fat(arr);

    do
    {
        int tmpcid = arr[cid];
        if (arr[cid] == LastCluster)
        {
            arr[cid] = Unused;
            break;
        }
        arr[cid] = Unused;

        cid = tmpcid;
    } while (1);

    write_fat(arr);
}

void create_disk(string sizekb, string filename)
{
    SECTOR_SIZE = stoi(sizekb) * 1024;

    boot.BS_JmpBoot = 0;
    strcpy(boot.BS_OEMName, "MSWIN4.1");

    boot.BPB_BytsPerSec = SECTOR_SIZE;
    boot.BPB_SecPerClus = 1;
    boot.BPB_RsvdSecCnt = 1;
    boot.BPB_NumFATs = 1;
    boot.BPB_RootEntCnt = 16 * (SECTOR_SIZE / 32);
    boot.BPB_FATSz16 = 15;

    fd = open(filename.c_str(), O_CREAT | O_RDWR, 0644);
    char buffer[SECTOR_SIZE];
    memset(buffer, 0, SECTOR_SIZE);
    for (int i = 0; i < SECTOR_COUNT; i++)
        write(fd, buffer, SECTOR_SIZE);

    lseek(fd, 0, SEEK_SET);
    memcpy(buffer, &boot, sizeof(BootSec));
    write(fd, buffer, sizeof(BootSec));

    lseek(fd, SECTOR_SIZE, SEEK_SET);

    int arr[FAT_SIZE];
    memset(arr, Unused, FAT_SIZE * sizeof(int));
    write(fd, arr, FAT_SIZE * sizeof(int));

    close(fd);
}

void recursive_traverser(int clusterid)
{

    int len = boot.BPB_RootEntCnt;
    struct DirEntry dirs[len];
    memset(dirs, 0, sizeof(DirEntry) * len);
    int size;
    int i = 0;
    if (clusterid == 4444)
    {
        read_root(dirs);
        size = size = find_dir_size(dirs, len);
    }
    else
    {
        i = 2;
        len = SECTOR_SIZE / sizeof(DirEntry);
        read_from_cluster(clusterid, dirs);
        size = find_dir_size(dirs, len);
    }

    if (size == 2 && i == 2)
    {
        return;
    }

    for (; i < size; i++)
    {
        if (dirs[i].Attributes == File)
        {

            int cid = dirs[i].FirstLogicalCluster;
            cout << dirs[i].Filename << " -> ";

            int arr[FAT_SIZE];
            read_fat(arr);

            do
            {
                int tmpcid = arr[cid];
                cout << cid;
                if (arr[cid] == LastCluster)
                {
                    cout << endl;

                    break;
                }
                cout << " -> ";
                cid = tmpcid;
            } while (1);
        }
        else
        {
            int cid = dirs[i].FirstLogicalCluster;
            cout << dirs[i].Filename << " -> " << cid << endl;

            recursive_traverser(dirs[i].FirstLogicalCluster);
        }
    }
}

void dumpe2fs()
{
    cout << "Sector size: " << boot.BPB_BytsPerSec << endl;
    cout << "Sector per Cluster: " << boot.BPB_SecPerClus << endl;
    cout << "Cluster count of root: " << boot.BPB_FATSz16 << endl;
    cout << "Entrt count of root: " << boot.BPB_RootEntCnt << endl;
    cout << "OEMName: " << boot.BS_OEMName << endl;
    cout << "Unused cluster count: " << count_unused() << endl;
    cout << "Used cluster count: " << FAT_SIZE - count_unused() << endl;
    recursive_traverser(4444);
}

void shell()
{

    do
    {
        vector<string> parse = get_line_parse(" ");

        if (parse.size() == 0)
            continue;
        if (parse[0] == std::string("exit"))
        {
            break;
        }
        if (parse[0] == "makeFileSystem")
        {
            if (parse.size() != 3)
                perror("shell,makefilesystem");

            create_disk(parse[1], parse[2]);
        }
        else if (parse[0] == "fileSystemOper")
        {

            fd = open(parse[1].c_str(), O_RDWR);

            if (fd == -1)
            {
                perror("shell,open");
                exit(0);
            }
            if (parse[2] == "mkdir")
            {
                mkdir(parse[3]);
            }
            else if (parse[2] == "rmdir")
            {
                rmdir(parse[3]);
            }
            else if (parse[2] == "dir")
            {
                dir(parse[3]);
            }
            else if (parse[2] == "write")
            {
                write_file(parse[3], parse[4]);
            }
            else if (parse[2] == "read")
            {
                read_file(parse[3], parse[4]);
            }
            else if (parse[2] == "del")
            {
                del(parse[3]);
            }
            else if (parse[2] == "dumpe2fs")
            {
                dumpe2fs();
            }
            close(fd);
        }

    } while (1);
}

int main(int argc, char **argv)
{
    shell();
    return 0;
}
