#include "rbfm.h"

const int maxPagesPerHeader = (PAGE_SIZE - 4) / sizeof(short);

const int MAX_RECORD_SIZE = PAGE_SIZE - 6 - 4;

RecordBasedFileManager* RecordBasedFileManager::_rbf_manager = NULL;

RecordBasedFileManager* RecordBasedFileManager::instance() {
	if (!_rbf_manager)
		_rbf_manager = new RecordBasedFileManager();

	return _rbf_manager;
}

RecordBasedFileManager::RecordBasedFileManager() {
	pfm = PagedFileManager::instance();
	pageBuffer = (char*) malloc(PAGE_SIZE);
	headerBuffer = (char*) malloc(PAGE_SIZE);
	recordBuffer = (char*) malloc(MAX_RECORD_SIZE);
	pageFreeSpace = -1;
	pageNum = -1;
	headerNum = -1;
//	pfm->printfileTracker();
}

RecordBasedFileManager::~RecordBasedFileManager() {
}

/*
 * This method creates a record-based file called fileName.The file should not
 * already exist. Please note that this method should internally use the method
 * PagedFileManager::createFile (const char *fileName).
 */
RC RecordBasedFileManager::createFile(const string &fileName) {
	return pfm->createFile(fileName);
}
/*
 * This method destroys the record-based file whose name is fileName. The file should
 *  exist. Please note that this method should internally use the method
 *  PagedFileManager::destroyFile (const char *fileName).
 */
RC RecordBasedFileManager::destroyFile(const string &fileName) {
	return pfm->destroyFile(fileName);
}

/*
 * This method opens the record-based file whose name is fileName. The file must already
 * exist and it must have been created using the RecordBasedFileManager::createFile method.
 * If the method is successful, the fileHandle object whose address is passed as a
 * parameter becomes a "handle" for the open file. The file handle rules in the method
 * PagedFileManager::openFile apply here too. Also note that this method should internally
 * use the method PagedFileManager::openFile(const char *fileName, FileHandle &fileHandle).
 */
RC RecordBasedFileManager::openFile(const string &fileName,
		FileHandle &fileHandle) {
	return pfm->openFile(fileName, fileHandle);
}

/*
 * This method closes the open file instance referred to by fileHandle. The file must have
 * been opened using the RecordBasedFileManager::openFile method. Note that this method should
 * internally use the method PagedFileManager::closeFile(FileHandle &fileHandle).
 */
RC RecordBasedFileManager::closeFile(FileHandle &fileHandle) {
	return pfm->closeFile(fileHandle);
}

RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle,
		const vector<Attribute> &recordDescriptor, const void *data, RID &rid) {

	/******************************************************************************
	 ***** TRANSLATE THE RECORD INTO NEW FORMAT AND COPY INTO RECORD BUFFER *******
	 ******************************************************************************/
	short attrNum = recordDescriptor.size();
	int nullsize = (int) ceil((double) attrNum / 8);

//	cout << "--------------------------------" << endl;
//	cout << "FROM insertRecord()" << endl;
//	cout << "nullsize = " << nullsize << endl;
//	cout << "attrNum = " << (int) attrNum << endl;

	unsigned char nullBuff[nullsize];
	memcpy(nullBuff, data, nullsize);

	//record format: number of attributes | null bits | offsets | fields

	unsigned char bit;
	unsigned char nullbyte;

	int index = 0;
	vector<int> indexes;

	// read the null bits, skip null fields, collect indexes
	// of non-null fields
	for (int i = 0; i < nullsize; ++i) {
		nullbyte = nullBuff[i];
		for (bit = (1 << 7); bit > 0; bit >>= 1) {

			if (index >= attrNum)
				goto quitfors;

			if ((nullbyte & bit) == 0) {

				indexes.push_back(index);

			}
			index++;
		}
	}
	quitfors:

	//copy the number of attributes
	int offset = 0;
	memcpy(recordBuffer, &attrNum, sizeof(short));
	offset += 2;

	//copy the null bits
	memcpy(recordBuffer + offset, data, nullsize);
	offset += nullsize;

	//copy the offsets
	short baseAttributesOffset = 1 + nullsize + indexes.size() * sizeof(short);

//	cout << "baseAttributsOffset = " << baseAttributesOffset << endl;

	short attributeOffset;
	short attributesLengthSum = 0;
	int length;
	Attribute attr;
	AttrType type;
	for (unsigned i = 0; i < indexes.size(); ++i) {
		index = indexes[i];
		attr = recordDescriptor[index];
		type = attr.type;
		length = attr.length;

		attributeOffset = baseAttributesOffset + attributesLengthSum;

		if (type == TypeVarChar) {

			int stringLength;
			memcpy(&stringLength, (char*) data + nullsize + attributesLengthSum,
					sizeof(int));
			attributesLengthSum += sizeof(int);
			length = stringLength;

		}

		memcpy(recordBuffer + offset, &attributeOffset, sizeof(short));
		offset += sizeof(short);

		attributesLengthSum += length;
	}

	//copy the fields
	memcpy(recordBuffer + baseAttributesOffset, (char*) data + nullsize,
			attributesLengthSum);

//	cout << "attributesLengthSum = " << attributesLengthSum << endl;
//	cout << "baseAttributesOffset = " << baseAttributesOffset << endl;

	//check that the record fits at least within a single page

	short recordSize = baseAttributesOffset + attributesLengthSum;

	if (recordSize > MAX_RECORD_SIZE) {
		cout << "ERROR: MAX_RECORD_SIZE = " << MAX_RECORD_SIZE
				<< " but this record has size = " << recordSize << endl;
		return -1;
	}

	/***************************************************************************************************
	 ***** INSERTING RECORD EITHER IN CURRENT WORKING PAGE OR IN ANOTHER ONE WITH ENOUGH SPACE   *******
	 ***************************************************************************************************/

	//if there is enough space in the current page
	if (pageFreeSpace >= recordSize + 4) {

//		cout <<"there is enough space:  pageFreeSpace = " <<  pageFreeSpace << " | recordSize = " << recordSize << endl;

		storeRecordInCurrentPage(recordSize, rid, fileHandle);

	} else { //else, there is not enough space in the page, so we search all the pages
		// until either we find a page with enough space or we get to the end of the file,
		// in which case we will have to append a new page at the end (and possibly
		// a new header page if the last header page was full).

//		cout <<"oops! there was not enough free space:  pageFreeSpace = " <<  pageFreeSpace << " | recordSize = " << recordSize << endl;

		int pageNumFound = fileHandle.findPageWithEnoughSpace(recordSize + 4);

		if (pageNumFound == -1) { //no page with enough space, we have to append a new page

			int numPages = fileHandle.getNumberOfPages();

			//if all the headers are full, we have to append a new header

//			cout
//					<< "\t----------------------------------------------------------------"
//					<< endl;
//			cout
//					<< "\tno page found with enough space. we will have to append a new page"
//					<< endl;

			headerNum = numPages / maxPagesPerHeader;
			pageNum = numPages;
			pageFreeSpace = PAGE_SIZE - recordSize - 6 - 4;

//			cout << "\trecordSize = " << recordSize << endl;
//			cout << "\theaderNum = " << headerNum << endl;
//			cout << "\tpageNum = " << pageNum << endl;
//			cout << "\tpageFreeSpace = " << pageFreeSpace << endl;

			//if the number of pages is positive and they
			//they are completing all the header pages, then
			//we need to append a new header page before appending
			//a new page
			if (numPages > 0 && numPages % maxPagesPerHeader == 0) {

				int numPages = 0;
				memcpy(headerBuffer, &numPages, sizeof(int));
				//append header at the end
				fileHandle.writeHeaderPage(headerNum, headerBuffer);

			}

			numPages++;

			//append a new page and store record there
			short freeSpaceOffset = recordSize;
			short slotsNumber = 1;
			short freeSlotIndex = -1;
			short recordOffset = 0;
			short recordLength = recordSize;
			memcpy(pageBuffer, recordBuffer, recordLength);
			memcpy(pageBuffer + PAGE_SIZE - 6, &freeSlotIndex, sizeof(short));
			memcpy(pageBuffer + PAGE_SIZE - 4, &slotsNumber, sizeof(short));
			memcpy(pageBuffer + PAGE_SIZE - 2, &freeSpaceOffset, sizeof(short));
			memcpy(pageBuffer + PAGE_SIZE - 10, &recordLength, sizeof(short));
			memcpy(pageBuffer + PAGE_SIZE - 8, &recordOffset, sizeof(short));
			fileHandle.appendPage(pageBuffer);

//			cout << "recordLength = " << recordLength << endl;

			//we have to update the number of pages in the last header
			//we have to update the free space for this page in the last header
			//we have to update the total number of pages in the first header

			//updating number of pages and this new page's free space in the last header
			fileHandle.readHeaderPage(headerNum, headerBuffer);
			int pageCount = numPages - headerNum * maxPagesPerHeader;
			memcpy(headerBuffer, &pageCount, sizeof(int));

//			cout << "pageCount = " << pageCount << endl;

			//
			int pageSlotOffset = 4
					+ sizeof(short) * (pageNum % maxPagesPerHeader);
			memcpy(headerBuffer + pageSlotOffset, &pageFreeSpace,
					sizeof(short));

//			cout << "pageSlotOffset = " << pageSlotOffset << endl;

			//
			fileHandle.writeHeaderPage(headerNum, headerBuffer);

			//set rid
			rid.pageNum = pageNum;
			rid.slotNum = 1;

//			cout << "rid.pageNum = " << rid.pageNum << endl;
//			cout << "rid.slotNum = " << rid.slotNum << endl;

		} else { //else, we were able to find an existing page with enough space, so let's use it

			pageNum = pageNumFound;
			headerNum = pageNum / maxPagesPerHeader;

//			cout << "--------------------" << endl;
//			cout << "We were able to find an existing page with enough space!" << endl;
//			cout << "pageNumFound = " << pageNumFound << endl;
//			cout << "headerNum " << headerNum << endl;

			//udpate page free space
			fileHandle.readHeaderPage(headerNum, headerBuffer);
			int pageSlotOffset = 4
					+ sizeof(short) * (pageNum % maxPagesPerHeader);
			memcpy(&pageFreeSpace, headerBuffer + pageSlotOffset,
					sizeof(short));

//			cout << "pageSlotOffset = " << pageSlotOffset << endl;
//			cout << "pageFreeSpace = " << pageFreeSpace << endl;

			//store record in the page found
			fileHandle.readPage(pageNum, pageBuffer);
			storeRecordInCurrentPage(recordSize, rid, fileHandle);

		}

	}

	return 0;
}

/*
 * Store the record (which is assumed to be stored in the recordBuffer) into the current page
 * (which is assumed to be cached in the pageBuffer).
 * Since we know that the page has enough free space, first we try to store the record
 * in the contiguous free space. If the contiguous free space is not big enough, we compact
 * all the previous records and then store the new record in the free space.
 */
void RecordBasedFileManager::storeRecordInCurrentPage(int recordSize, RID& rid,
		FileHandle& fileHandle) {

//	cout << "\t" << "------------------------------ " << endl;
//	cout << "\t" << "from rbfm:storeRecordInCurrentPage() " << endl;

	//check if there is enough contiguous free space to store the record

	short freeSpaceOffset;
	short slotsNumber;
	short firstFreeSlotIndex;

	bool noFreeSlot;

	memcpy(&freeSpaceOffset, pageBuffer + PAGE_SIZE - 2, sizeof(short));
	memcpy(&slotsNumber, pageBuffer + PAGE_SIZE - 4, sizeof(short));
	memcpy(&firstFreeSlotIndex, pageBuffer + PAGE_SIZE - 6, sizeof(short));

	int contiguousFreeSpace = PAGE_SIZE - freeSpaceOffset - 6
			- slotsNumber * 2 * sizeof(short);

//	cout << "\tfreeSpaceOffset = " << freeSpaceOffset << endl;
//	cout << "\tslotsNumber = " << slotsNumber << endl;
//	cout << "\tfirstFreeSlotIndex = " << firstFreeSlotIndex << endl;


	noFreeSlot = (firstFreeSlotIndex == -1);

//	cout << "\tnoFreeSlot = " << noFreeSlot << endl;

	//we check if there is any free slot we can reuse, otherwise we have to append a new
	// slot at the bottom to store the address and the length of this new record
	if (noFreeSlot)
		contiguousFreeSpace -= sizeof(short) * 2;

	//if there is enough contiguous free space
	//we store the record at the beginning of it
	if (contiguousFreeSpace >= recordSize) {

//		cout << "\t" << " there is enough contiguous free space, yay! " << endl;

		//copy the record
		memcpy(pageBuffer + freeSpaceOffset, recordBuffer, recordSize);

		//copy the record metadata either in a new slot or in a free slot
		if (noFreeSlot) {
//			cout << "\t\t" << " no free slot -> we have to use a new one " << endl;
			slotsNumber++;
			int newSlotOffset = PAGE_SIZE - 6 - slotsNumber * 2 * sizeof(short);
			//copy the record size
			memcpy(pageBuffer + newSlotOffset, &recordSize, sizeof(short));
			//copy the record offset
			memcpy(pageBuffer + newSlotOffset + 2, &freeSpaceOffset,
					sizeof(short));
			//update slotsNumber
			memcpy(pageBuffer + PAGE_SIZE - 4, &slotsNumber, sizeof(short));

			//set slot number in rid
			rid.slotNum = slotsNumber;

		} else { //we can reuse a free slot
//			cout << "\t\t" << " there is a free slot -> let's use it" << endl;
			int newSlotOffset = PAGE_SIZE - 6
					- firstFreeSlotIndex * 2 * sizeof(short);
			//copy the record size
			memcpy(pageBuffer + newSlotOffset, &recordSize, sizeof(short));
			//copy the record offset
			memcpy(pageBuffer + newSlotOffset + 2, &freeSpaceOffset,
					sizeof(short));

			//we have to find a new free slot and update firstFreeSlotIndex in the page
			firstFreeSlotIndex = -1;
			short aux;
			for (int index = 1, offset = PAGE_SIZE - 6 - 2;
					index <= slotsNumber; ++index, offset -= 4) {
				//read the slotOffset
				memcpy(&aux, pageBuffer + offset, sizeof(short));
				if (aux == -1) { //free slot
					firstFreeSlotIndex = index;
					break;
				}
			}
			memcpy(pageBuffer + PAGE_SIZE - 6, &firstFreeSlotIndex,
					sizeof(short));

			//set slot number in rid
			rid.slotNum = firstFreeSlotIndex;
		}

		//update freeSpaceOffset at the end of the page
		freeSpaceOffset += recordSize;
		memcpy(pageBuffer + PAGE_SIZE - 2, &freeSpaceOffset, sizeof(short));

	} else { //there is not enough contiguous space, so we have to compact the records

//		cout << "\t" << "not enough contiguous free space -> we are going to compact records" << endl;

		//index,length,offset per record
		vector<pair<int, pair<short, short> > > indexSlotDataPairs;

		short recordLength;
		short recordOffset;

		for (int i = 1, offset = PAGE_SIZE - 6 - 4; i <= slotsNumber;
				++i, offset -= 4) {
			memcpy(&recordLength, pageBuffer + offset, sizeof(short));
			memcpy(&recordOffset, pageBuffer + offset + 2, sizeof(short));

			if (recordOffset == -1)
				continue;

			indexSlotDataPairs.push_back(
					std::make_pair(i,
							std::make_pair(recordLength, recordOffset)));
		}

		//sort in increasing order based on recordOffset
		sort(indexSlotDataPairs.begin(), indexSlotDataPairs.end(), pairCompare);

		short offset = 0;
		int index;

		//compact
		for (unsigned i = 0; i < indexSlotDataPairs.size(); ++i) {

			index = indexSlotDataPairs[i].first;
			recordLength = indexSlotDataPairs[i].second.first;
			recordOffset = indexSlotDataPairs[i].second.second;

			if (offset < recordOffset) {
				memmove(pageBuffer + offset, pageBuffer + recordOffset,
						recordLength);
				memmove(pageBuffer + PAGE_SIZE - 6 - 4 * index + 2, &offset,
						sizeof(short));
			}

			offset += recordLength;
		}

		//store the record at the beginning of the now compacted free space
		memcpy(pageBuffer + offset, recordBuffer, recordSize);

		//copy the record metadata either in a new slot or in a free slot
		if (noFreeSlot) {
			slotsNumber++;
			int newSlotOffset = PAGE_SIZE - 6 - slotsNumber * 2 * sizeof(short);
			//copy the record size
			memcpy(pageBuffer + newSlotOffset, &recordSize, sizeof(short));
			//copy the record offset
			memcpy(pageBuffer + newSlotOffset + 2, &offset, sizeof(short));
			//update slotsNumber
			memcpy(pageBuffer + PAGE_SIZE - 4, &slotsNumber, sizeof(short));

			//set slot number in rid
			rid.slotNum = slotsNumber;

		} else { //we can reuse a free slot

			int newSlotOffset = PAGE_SIZE - 6
					- firstFreeSlotIndex * 2 * sizeof(short);
			//copy the record size
			memcpy(pageBuffer + newSlotOffset, &recordSize, sizeof(short));
			//copy the record offset
			memcpy(pageBuffer + newSlotOffset + 2, &offset, sizeof(short));

			//we have to find a new free slot and update firstFreeSlotIndex in the page
			firstFreeSlotIndex = -1;
			short aux;
			for (int index = 1, offset = PAGE_SIZE - 6 - 2;
					index <= slotsNumber; ++index, offset -= 4) {
				//read the slotOffset
				memcpy(&aux, pageBuffer + offset, sizeof(short));
				if (aux == -1) { //free slot
					firstFreeSlotIndex = index;
					break;
				}
			}
			memcpy(pageBuffer + PAGE_SIZE - 6, &firstFreeSlotIndex,
					sizeof(short));

			//set slot number in rid
			rid.slotNum = firstFreeSlotIndex;
		}

		//update freeSpaceOffset at the end of the page
		offset += recordSize;
		memcpy(pageBuffer + PAGE_SIZE - 2, &offset, sizeof(short));

	}

	//write the pageBuffer back to the page on disk
	fileHandle.writePage(pageNum,pageBuffer);

	//set page number in rid
	rid.pageNum = pageNum;

	//update page free space in its corresponding headerPage (considering whether we reused a slot or not)
	pageFreeSpace -= recordSize + (noFreeSlot ? 4 : 0);
	fileHandle.readHeaderPage(headerNum, headerBuffer);
	int pageSlotOffset = 4 + (pageNum % maxPagesPerHeader) * sizeof(short);
	memcpy(headerBuffer + pageSlotOffset, &pageFreeSpace, sizeof(short));
	fileHandle.writeHeaderPage(headerNum, headerBuffer);
}

bool RecordBasedFileManager::pairCompare(
		const pair<int, pair<short, short> >& firstElem,
		const pair<int, pair<short, short> >& secondElem) {
	return firstElem.second.second < secondElem.second.second;
}

/*
 * Given a record descriptor, read the record identified by the given rid.
 */
RC RecordBasedFileManager::readRecord(FileHandle &fileHandle,
		const vector<Attribute> &recordDescriptor, const RID &rid, void *data) {

	//check that rid points to an existing record

//	cout << "-------------------------------" << endl;
//	cout << " from rbfm:readRecord()" << endl;

	unsigned totalPages = fileHandle.getNumberOfPages();

//	cout << "rid.pageNum = " << rid.pageNum << " | rid.slotNum = " << rid.slotNum << endl;
//
//	cout << "totalPages = " << totalPages << endl;

	if (totalPages <= rid.pageNum || rid.pageNum < 0) {
		cout << "rid.pageNum = " << rid.pageNum
				<< " points to a nonexistent page" << endl;
		return -1;
	}

	pageNum = rid.pageNum;
	headerNum = pageNum / maxPagesPerHeader;

//	cout << "pageNum = " << pageNum << endl;
//	cout << "headerNum = " << headerNum << endl;

	fileHandle.readPage(pageNum, pageBuffer);

	short slotsNumber;
	memcpy(&slotsNumber, pageBuffer + PAGE_SIZE - 4, sizeof(short));

//	cout << "slotsNumber = " << slotsNumber << endl;

	if (slotsNumber < rid.slotNum || rid.slotNum < 1) {
		cout << "rid.slotNum = " << rid.slotNum
				<< " points to a nonexistent slot" << endl;
		return -1;
	}

	//copy the record into recordBuffer
	short recordLength;
	short recordOffset;
	int slotOffset = PAGE_SIZE - 6 - rid.slotNum * 4;
	memcpy(&recordLength, pageBuffer + slotOffset, sizeof(short));
	memcpy(&recordOffset, pageBuffer + slotOffset + 2, sizeof(short));

//	cout << "recordLength = " << recordLength << endl;
//	cout << "recordOffset = " << recordOffset << endl;
//	cout << "slotOffset = " << slotOffset << endl;

	memcpy(recordBuffer, pageBuffer + recordOffset, recordLength);

	//change record format and copy into data
	short attrNum;
	memcpy(&attrNum, recordBuffer, sizeof(short));

//	cout << "attrNum = " << attrNum << endl;

	int nullsize = (int) ceil((double) attrNum / 8);
	unsigned char nullbits[nullsize];
	memcpy(nullbits, recordBuffer + sizeof(short), nullsize);

//	cout << "nullsize = " << nullsize << endl;

	unsigned char bit;
	unsigned char nullbyte;

	int index = 0;
	vector<int> nonNullAttrIndexes;

	// read the null bits, skip null fields, collect indexes
	// of non-null fields
	for (int i = 0; i < nullsize; ++i) {
		nullbyte = nullbits[i];
		for (bit = (1 << 7); bit > 0; bit >>= 1) {

			if (index >= attrNum)
				goto quitfors;

			if ((nullbyte & bit) == 0) {

				nonNullAttrIndexes.push_back(index);

			}
			index++;
		}
	}
	quitfors:

//	cout << "non null attr: " <<nonNullAttrIndexes.size() << endl;

	short baseAttributesOffset = 1 + nullsize
			+ nonNullAttrIndexes.size() * sizeof(short);
	short attributeOffset;
	short attributesLengthSum = 0;
	int length;
	Attribute attr;
	AttrType type;

//	cout << "baseAttributesOffset = " << baseAttributesOffset << endl;

	for (unsigned i = 0; i < nonNullAttrIndexes.size(); ++i) {
		index = nonNullAttrIndexes[i];
		attr = recordDescriptor[index];
		type = attr.type;
		length = attr.length;

		if (type == TypeVarChar) {

			int stringLength;
			attributeOffset = baseAttributesOffset + attributesLengthSum;
			memcpy(&stringLength, recordBuffer + attributeOffset, sizeof(int));
			attributesLengthSum += sizeof(int);
			length = stringLength;

		}

		attributesLengthSum += length;
	}

	//copy null bits
	memcpy((char*) data, nullbits, nullsize);

	//copy the fields at the end of data
	memcpy((char*) data + nullsize, recordBuffer + baseAttributesOffset,
			attributesLengthSum);
	return 0;
}

/*
 * This is a utility method that will be mainly used for debugging/testing. It should be
 * able to interpret the bytes of each record using the passed-in record descriptor and
 * then print its content to the screen. For instance, suppose a record consists of two
 * fields: age (int) and height (float), which means the record will be of size 9 (1 byte
 * for the null-fields-indicator, 4 bytes for int, and 4 bytes for float). The printRecord
 * method should recognize the record format using the record descriptor. It should then
 * check the null-fields-indicator to skip certain fields if there are any NULL fields.
 * Then, it should be able to convert the four bytes after the first byte into an int
 * object and the last four bytes to a float object and print their values. It should also
 * print NULL for those fields that are skipped because they are null. Thus, an example for
 * three records would be:
 *  age: 24      height: 6.1
 *  age: NULL    height: 7.5
 *	age: 32      height: NULL
 */
RC RecordBasedFileManager::printRecord(
		const vector<Attribute> &recordDescriptor, const void *data) {

	stringstream ss;
	int attrNum = recordDescriptor.size();
	int nullsize = (int) ceil((double) attrNum / 8);
	unsigned char nullbytes[nullsize];
	memcpy(nullbytes, data, nullsize);
	unsigned char nullbyte;
	int index = 0;
	AttrType type;
	AttrLength length;

	int offset = nullsize;

	for (int i = 0; i < nullsize; ++i) {
		nullbyte = nullbytes[i];
		for (unsigned char b = 1 << 7; b > 0; b >>= 1) {

			if (index >= attrNum)
				goto exitLoops;
			ss << recordDescriptor[index].name << ": ";

			if ((nullbyte & b) == 0) { //the field is not null

				//TypeInt = 0, TypeReal, TypeVarChar
				type = recordDescriptor[index].type;
				length = recordDescriptor[index].length;

				if (type == TypeInt) {
					int field;
					memcpy(&field, (char*) data + offset, length);
					ss << field;
				} else if (type == TypeReal) {
					float field;
					memcpy(&field, (char*) data + offset, length);
					ss << field;
				} else if (type == TypeVarChar) {
					int stringLength;
					memcpy(&stringLength, (char*) data + offset, sizeof(int));
					offset += sizeof(int);
					char field[stringLength];
					memcpy(field, (char*) data + offset, stringLength);
					length = stringLength;
					ss << string(field, field + stringLength);
				} else {
					cout
							<< "ERROR: type doesn't match any of the valid types defined "
							<< endl;
				}
				ss << "\t";
				offset += length;
			} else {
				ss << "NULL\t";
			}
			index++;
		}
	}
	exitLoops:

	cout << ss.str() << endl;

	return 0;
}
