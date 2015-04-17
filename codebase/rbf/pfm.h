#ifndef _pfm_h_
#define _pfm_h_

typedef int RC;
typedef char byte;
typedef unsigned PageNum;

#define PAGE_SIZE 4096
#include <string>
#include <climits>
#include <cstdio>
#include <map>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <cmath>

using namespace std;

class FileHandle;

class PagedFileManager {
public:
	static PagedFileManager* instance();   // Access to the _pf_manager instance

	RC createFile(const string &fileName);                  // Create a new file
	RC destroyFile(const string &fileName);                    // Destroy a file
	RC openFile(const string &fileName, FileHandle &fileHandle); // Open a file
	RC closeFile(FileHandle &fileHandle);                        // Close a file

	void printfileTracker();

protected:
	PagedFileManager();                                   // Constructor
	~PagedFileManager();                                  // Destructor

private:
	static PagedFileManager *_pf_manager;
	map<string, int> fileTracker; // file name -> handle counter
	void initializefileTracker();
	bool FileExists(const string & fileName);
};

class FileHandle {
public:

	// variables to keep counter for each operation
	unsigned readPageCounter;
	unsigned writePageCounter;
	unsigned appendPageCounter;

	FileHandle();                                         // Default constructor
	~FileHandle();                                                 // Destructor

	RC readPage(PageNum pageNum, void *data);             // Get a specific page
	RC writePage(PageNum pageNum, const void *data);    // Write a specific page
	RC appendPage(const void *data);                   // Append a specific page
	unsigned getNumberOfPages();          // Get the number of pages in the file
	RC collectCounterValues(unsigned &readPageCount, unsigned &writePageCount,
			unsigned &appendPageCount); // put the current counter values into variables
	bool hasOpenFile();
	void setFileName(const string & fileName);
	string getFileName();
	void openFile();
	void closeFile();

	void readHeaderPage(int headerNum, void *data);
	void writeHeaderPage(int headerNum, const void * data);
	int findPageWithEnoughSpace(int requiredSpace);

private:
	unsigned pageCount; //number of pages
	string fileName; //name of the file this handle is handling
	FILE * file; //pointer to the file
};

#endif
