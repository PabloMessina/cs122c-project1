#include "pfm.h"

PagedFileManager* PagedFileManager::_pf_manager = 0;

const int maxPagesPerHeader = (PAGE_SIZE - 4) / sizeof(short);

PagedFileManager* PagedFileManager::instance() {
	if (!_pf_manager)
		_pf_manager = new PagedFileManager();

	return _pf_manager;
}

PagedFileManager::PagedFileManager() {

}

PagedFileManager::~PagedFileManager() {
}

bool PagedFileManager::FileExists(const string &fileName) {
	struct stat stFileInfo;
	if (stat(fileName.c_str(), &stFileInfo) == 0)
		return true;
	else
		return false;
}

/*
 * This method creates an empty-paged file called fileName.
 * The file should not already exist. This method should not
 * create any pages in the file.
 */
RC PagedFileManager::createFile(const string &fileName) {

	//check that the file doesn't exist yet
	if (FileExists(fileName))
		return -1;

	//create the file
	FILE* file = fopen(fileName.c_str(), "wb");
	if (file != NULL) {

		//register the page in the header file

		//register the file in the fileTracker
		fileTracker[fileName] = 0;

		//create the first header page of the file
		//by default (note that this is not a record page, just the
		//first header page)
		char* buff = (char*)malloc(PAGE_SIZE);
		int pageCount = 0;
		memcpy(buff, &pageCount, sizeof(int));
		fwrite(buff, 1, PAGE_SIZE, file);
		free(buff);

		//close the file
		fclose(file);

		return 0;
	}

	return -1;
}

/*
 * This method destroys the paged file whose name is fileName.
 * The file should already exist.
 */
RC PagedFileManager::destroyFile(const string &fileName) {
	map<string, int >::iterator it;
	if ((it = fileTracker.find(fileName)) != fileTracker.end()
			&& it->second == 0) {
//		cout << "file " << fileName << " will be deleted" << endl;
		fileTracker.erase(it->first);
		return remove(fileName.c_str());
	}
	cout << "file " << fileName
			<< " was not found in the fileTracker or there is at least one fileHandle using it"
			<< endl;
	return -1;
}

/*
 * This method opens the paged file whose name is fileName. The file must already exist
 * (and been created using the createFile method). If the open method is successful, the
 * fileHandle object whose address is passed in as a parameter now becomes a "handle" for
 * the open file. This file handle is used to manipulate the pages of the file (see the
 * FileHandle class description below). It is an error if fileHandle is already a handle
 * for some open file when it is passed to the openFile method. It is not an error to open
 * the same file more than once if desired, but this would be done by using a different
 * fileHandle object each time. Each call to the openFile method creates a new "instance"
 * of the open file. Warning: Opening a file more than once for data modification is not
 * prevented by the PF component, but doing so is likely to corrupt the file structure and
 * may crash the PF component. (You do not need to try and prevent this, as you can assume
 * the layer above is "friendly" in that regard.) Opening a file more than once for reading
 * is no problem.
 */
RC PagedFileManager::openFile(const string &fileName, FileHandle &fileHandle) {
	if(!FileExists(fileName)) {
		cout << "ERROR: the file " <<fileName <<" does not exist" << endl;
		return -1;
	}

	if(fileHandle.hasOpenFile()) {
		cout << "ERROR: the file handle is already busy reading the file " << fileHandle.getFileName() << endl;
		return -1;
	}

	//register file in the file tracker if it was not already there
	if(fileTracker.find(fileName) == fileTracker.end()) {
		fileTracker[fileName] = 0;
	}

	fileHandle.setFileName(fileName);
	fileHandle.openFile();
	fileTracker[fileName]++; //increase the handle counter associated to this file
	return 0;
}

/*
 * This method closes the open file instance referred to by fileHandle. (The file should have
 *  been opened using the openFile method.) All of the file's pages are flushed to disk when
 *   the file is closed.
 */
RC PagedFileManager::closeFile(FileHandle &fileHandle) {
	if (fileHandle.hasOpenFile()) {
		//close file
		fileHandle.closeFile();
		//decrease the handle counter associated with the file
		fileTracker[fileHandle.getFileName()]--;
		return 0;
	}
	return -1;
}

void PagedFileManager::printfileTracker() {
	int handleCounter;
	string name;
	for (std::map<string,int >::iterator it =
			fileTracker.begin(); it != fileTracker.end(); ++it) {
		name = it->first;
		handleCounter = it->second;
		cout << "fileTracker[" << name << "] -> " << "handles="
				<< handleCounter << endl;
	}
}

FileHandle::FileHandle() {
	readPageCounter = 0;
	writePageCounter = 0;
	appendPageCounter = 0;
	pageCount = 0;
	file = NULL;
}

FileHandle::~FileHandle() {
}

/*
 * This method reads the page into the memory block pointed to by data.
 * The page should exist. Note that page numbers start from 0.
 * Here is a part of some example code for readPage that increases readPageCount
 * whenever it is executed. For writePage() and appendPage(), the logic is similar.
 * RC FileHandle::readPage(PageNum pageNum, void *data) {
 *	......
 *	readPageCount = readPageCount + 1;
 *	return 0;
 *}
 */
RC FileHandle::readPage(PageNum pageNum, void *data) {

	if (file != NULL) {

		//refresh the pageCount with the pageCount stored
		//in the first header page of the file
		fseek(file, 0, SEEK_SET);
		fread(&(this->pageCount), sizeof(int), 1, file);

		//check that the page exists
		if (pageCount <= pageNum) {
			cout
					<< "Read failed. Trying to  access a pageNum beyond the current range ( "
					<< pageCount << " )" << endl;
			return -1;
		}

		int pageOffset = ((pageNum / maxPagesPerHeader)
				* (maxPagesPerHeader + 1) + (pageNum % maxPagesPerHeader + 1))
				* PAGE_SIZE;

		fseek(file, pageOffset, SEEK_SET);
		fread(data, 1, PAGE_SIZE, file);

		this->readPageCounter++;

		return 0;

	}

	return -1;
}

/*
 * This method writes the given data into a page specified by pageNum.
 * The page should exist. Page numbers start from 0.
 */
RC FileHandle::writePage(PageNum pageNum, const void *data) {

	if (file != NULL) {

		//refresh the pageCount with the pageCount stored
		//in the first header page of the file
		int pageCount = getNumberOfPages();

		//check that the page exists
		if (pageCount <= pageNum) {
			cout
					<< "Read failed. Trying to  access a pageNum beyond the current range ( "
					<< pageCount << " )" << endl;
			return -1;
		}

		int pageOffset = ((pageNum / maxPagesPerHeader)
				* (maxPagesPerHeader + 1) + (pageNum % maxPagesPerHeader + 1))
				* PAGE_SIZE;

		fseek(file, pageOffset, SEEK_SET);
		fwrite(data, 1, PAGE_SIZE, file);

		this->writePageCounter++;

		return 0;
	}

	return -1;
}

/*
 * This method appends a new page to the end of the file and writes
 *  the given data into the newly allocated page.
 */
RC FileHandle::appendPage(const void *data) {
	if (file != NULL) {
		fseek(file, 0, SEEK_END);
		fwrite(data, 1, PAGE_SIZE, file);
		this->appendPageCounter++;

		//updating total number of pages in the first header
		int pageCount = getNumberOfPages();
		fseek(file, 0, SEEK_SET);
		pageCount++;
		fwrite(&pageCount,sizeof(int),1,file);

		return 0;
	}
	return -1;
}

void FileHandle::readHeaderPage(int headerNum, void * data) {
//	cout << "------------------" << endl;
//	cout << "from fileHandle:: readHeaderPage()" << endl;

	if (file != NULL) {
		int headerPageOffset = headerNum * (maxPagesPerHeader + 1) * PAGE_SIZE;

//		cout << "headerPageOffset = " << headerPageOffset << endl;

		fseek(file, headerPageOffset, SEEK_SET);
		fread(data, 1, PAGE_SIZE, file);
	}
}

void FileHandle::writeHeaderPage(int headerNum, const void * data) {
//	cout << "-----------------------" << endl;
//	cout << "from filehandle::writeHeaderPage()" << endl;

	if (file != NULL) {
		int headerPageOffset = headerNum * (maxPagesPerHeader + 1) * PAGE_SIZE;

//		cout << "headerPageOffset = " << headerPageOffset << endl,

		fseek(file, headerPageOffset, SEEK_SET);
		fwrite(data, 1, PAGE_SIZE, file);
	}
}

/*
 * Returns the pageNum of the first page with enough space found (if any),
 * or -1 by default.
 */
int FileHandle::findPageWithEnoughSpace(int requiredSpace) {

	if (file != NULL) {

		int totalPages = getNumberOfPages();

//		cout << "------------------------" << endl;
//		cout << "from findPageWithEnoughSpace() " << endl;
//
//		cout << "requiredSpace = " << requiredSpace << endl;
//
//		cout << "totalPages = " << totalPages << endl;


		int totalHeaders = (int)ceil((double)totalPages / maxPagesPerHeader);
		int pn = 0;

//		cout << "totalHeaders = " << totalHeaders << endl;

		int pageSlotOffset;
		int headerPageOffset = 0;
		short freeSpace;

		int FULL_HEADER_SCOPE_SIZE = (maxPagesPerHeader + 1) * PAGE_SIZE;

		for(int hn = 0; hn < totalHeaders; ++hn) {
//			cout << "\theaderPageOffset = " << headerPageOffset << endl;
			for(int j = 0; j < maxPagesPerHeader; ++j) {
				if(pn >= totalPages) {
//					cout << "no page with enough space was found among a total of " << totalPages <<" pages" << endl;
					return -1;
				}
				pageSlotOffset = headerPageOffset + 4 + j * sizeof(short);
				fseek(file,pageSlotOffset,SEEK_SET);
				fread(&freeSpace,sizeof(short),1,file);

//				cout << "\t\tpageSlotOffset = " << pageSlotOffset << endl;
//				cout << "\t\tfreeSpace = " << freeSpace << endl;

				if(freeSpace >= requiredSpace) {

//					cout << "freeSpace = "<<freeSpace<<" found in page " <<pn << " !!"<< endl;

					return pn;
				}
				pn++;
			}
			headerPageOffset += FULL_HEADER_SCOPE_SIZE;
		}

//		cout << "No page with enough space has been found " << endl;
		return -1;

	}

	cout << " ERROR: file is not open" << endl;
	return -1;
}

unsigned FileHandle::getNumberOfPages() {
//refresh the pageCount with the pageCount stored
//in the first header page of the file
	if (file != NULL) {
		fseek(file, 0, SEEK_SET);
		fread(&(this->pageCount), sizeof(int), 1, file);
	}
	return pageCount;
}

RC FileHandle::collectCounterValues(unsigned &readPageCount,
		unsigned &writePageCount, unsigned &appendPageCount) {
	readPageCount = this->readPageCounter;
	writePageCount = this->writePageCounter;
	appendPageCount = this->appendPageCounter;
	return 0;
}

bool FileHandle::hasOpenFile() {
	return file != NULL;
}

void FileHandle::setFileName(const string & fileName) {
	this->fileName = fileName;
}

string FileHandle::getFileName() {
	return this->fileName;
}

void FileHandle::openFile() {
	if (file == NULL) {
		file = fopen(fileName.c_str(), "rb+");
	}
}

void FileHandle::closeFile() {
	if (file != NULL) {
		fclose(file);
		file = NULL;
	}
}
