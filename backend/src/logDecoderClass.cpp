#include "globals.h"
#include "logDecoderClass.h"
#include "helperFunctions.h"

//logDecoderClass.h class function definitions

//Constructor for fileDecodingInfo class
//Parses ATO/ATP config files if necessary to create ATO/ATP globals, otherwise calls them by reference, and initializes them to all other files
fileDecodingInfo::fileDecodingInfo(struct fileInfo* fileInfoStruct, int logType) : fileInfoStruct(fileInfoStruct) {
	//If log is of ATP type
	if( ATP_NUM == logType )
	{
		this->byteNumToSkip = ATP_BYTE_NUM_TO_SKIP - 1; 
		this->byteNumForLine = MAX_ATP_PARAMS_BIT_SIZE/8;

		if(NULL != ATP_parameterInfo)
		{
			this->parameterInfoVec = *ATP_parameterInfo;
		}
		else
		{
			parseLabelsConfigFile(ATP_EnumeratedLabels,logType,fileInfoStruct->directoryPath);

			parseParameterConfigFile(this->parameterInfoVec,logType,fileInfoStruct->directoryPath,ATP_EnumeratedLabels,ATP_StringLabels);
			ATP_parameterInfo = &(this->parameterInfoVec); 
		}
		this->stringLabels = ATP_StringLabels;
		this->enumeratedLabels = ATP_EnumeratedLabels;
	}
	//if log is of ATO type
	else
	{
		this->byteNumToSkip = ATO_BYTE_NUM_TO_SKIP - 1; 
		this->byteNumForLine = MAX_ATO_PARAMS_BIT_SIZE/8;

		if(NULL != ATO_parameterInfo)
		{
			this->parameterInfoVec = *ATO_parameterInfo;
		}
		else
		{
			parseLabelsConfigFile(ATO_EnumeratedLabels,logType,fileInfoStruct->directoryPath);

			parseParameterConfigFile(this->parameterInfoVec,logType,fileInfoStruct->directoryPath,ATO_EnumeratedLabels,ATO_StringLabels);
			ATO_parameterInfo = &(this->parameterInfoVec); 
		}
		this->stringLabels = ATO_StringLabels;
		this->enumeratedLabels = ATO_EnumeratedLabels;
	}
}

//Destructor for fileDecodingInfo class 
//If file had to be decoded, then its .txt equivalent will be removed
fileDecodingInfo::~fileDecodingInfo(){
	char filePath[MAX_STRING_SIZE];
	sprintf(filePath,"%s_%s%s",this->fileInfoStruct->directoryPath,this->fileInfoStruct->fileName,TXT_SUFFIX);
	if ( 0 == access(filePath, F_OK) ) 
	{
		remove(filePath);
	}
	else
	{
		//do nothing
	}
	free(this->fileInfoStruct);
}

//fileDecodingInfo getter functions:

//Returns fileType (processed .txt vs raw OMAP log)
int fileDecodingInfo::getFileType(){
	return this->fileInfoStruct->fileType;
}
//Returns file core (1 vs 2)
int fileDecodingInfo::getCoreType(){
	return this->fileInfoStruct->core;
}
//Returns logtype of file (ATO vs ATP)
int fileDecodingInfo::getLogType(){
	return this->fileInfoStruct->logType;
}
//Returns FILE* pointer to output file
FILE* fileDecodingInfo::getOutputFile(){
	return this->fileInfoStruct->outputFile;
}
//Returns parent directory of file
char* fileDecodingInfo::getDirectoryPath(){
	return this->fileInfoStruct->directoryPath;
}
//Returns filename of initially uploaded file
char* fileDecodingInfo::getFileName(){
	return this->fileInfoStruct->fileName;
}
//Returns filename of output filename
char* fileDecodingInfo::getOutputFileName(){
	return this->fileInfoStruct->outputFileName;
}

//Takes the raw OMAP log and decodes it to a .txt version
//3 phases : 1) SkipSequence 2) getHeaderSequence 3) getLineSequence
// 1)Skips all characters until the OMNIPOTENT SET is found (this set is constant and the delimeter between all records)
// 2)Gets 19 header bytes in order to write the appropriate timestamp
// 3)Gets bytes for a full line, then decodes the concatenated line
//Void return value because all of the output is printed to a file
void fileDecodingInfo::decodeFile(){
	struct headerInfo* headerStruct = &(this->headerInfoStruct);
	int logType = this->fileInfoStruct->logType;

	int numParameters = (logType == ATO_NUM) ? MAX_ATO_PARAMS : MAX_ATP_PARAMS;
	int numLineBits = (logType == ATO_NUM) ? MAX_ATO_PARAMS_BIT_SIZE : MAX_ATP_PARAMS_BIT_SIZE;
	int paramsCharSize = this->byteNumForLine;
	int headerCharSize = headerStruct->headerByteSize;
	int skipCharSize = 1;

	char curParams[numParameters][MAX_SHORT_STRING_SIZE + 1];	
	char curLine[numLineBits + 1];
	char curHeader[headerStruct->headerBitSize + 1];
	char curSkipLine[MAX_SHORT_STRING_SIZE];
	curLine[0] = '\0';
	curHeader[0] = '\0';

	char* headerP = curHeader;
	char* lineP = curLine;
	unsigned int skipSeqNum = 0;
	int curChar;
	
	printHeader(numParameters);

	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
	while( EOF != curChar ){
		curChar = fgetc(this->fileInfoStruct->inputFile);

		if(skipCharSize)
		{
			if(skipSeq(curChar,skipSeqNum))
			{
				if( OMNIPOTENT_SET == skipSeqNum )
				{
					skipCharSize = 0;
				}
				else
				{
					skipSeqNum = 0;	
				}

			}
			else
			{
				//do nothing
			}
		}
		else if(headerCharSize)
		{
			headerP = fast_strcat(headerP,byteArray[curChar]);	
			headerCharSize--;
		}
		else if(paramsCharSize)
		{
			lineP = fast_strcat(lineP,byteArray[curChar]);
			paramsCharSize--;
		}
		else
		{
			decodeLine(curHeader,curLine,curParams);
			printLine(curParams,numParameters);
			//Re-initialize -- Skip over nullish sequence
			memset(curHeader,0,headerStruct->headerBitSize);
			memset(curLine,0,numLineBits);
			headerP = curHeader;
			lineP = curLine;
			skipSeqNum = 0;
			skipCharSize = 1;

			headerCharSize = headerStruct->headerByteSize;
			paramsCharSize = this->byteNumForLine;
		}
	}
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	std::cout << "Time To Decode = " << (double) std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()/ 1000000 << "[s]" << std::endl;

	//Swap FILE* pointers
	swapFilePointers();
}

//Takes the binary strings curHeader & curLine, and parses them according to the bit-positions and bit-counts of the ATO/ATP parameters
//After getting the truncated binary string PER parameter, a member function pointer is called to do the appropriate parsing to convert it from a binary string to ASCII text
//Function pointer optimizes this so that no branch conditioning is needed during the ACTUAL parsing
//Void return type because the output is directly returned to curParams array
void fileDecodingInfo::decodeLine(char* curHeader, char* curLine, char (*curParams)[MAX_SHORT_STRING_SIZE + 1]){
	//Get timestamp for first 2 params
	struct headerInfo* headerStruct = &(this->headerInfoStruct);
	char binaryParam[MAX_SHORT_STRING_SIZE + 1];
	char resultStr[MAX_SHORT_STRING_SIZE];
	class parameterInfo* parameterObj = this->parameterInfoVec[0];

	memcpy(binaryParam,curHeader + headerStruct->timeBitPos, headerStruct->timeBitSize);
	binaryParam[headerStruct->timeBitSize] = '\0';

	(parameterObj->*(parameterObj->binaryToString))(binaryParam,curParams[0]);

	parameterObj = this->parameterInfoVec[1];
	(parameterObj->*(parameterObj->binaryToString))(binaryParam,curParams[1]);
	strcat(curParams[1],HEADER_TIME_SUFFIX);
	
	for(int i = 2; i < this->parameterInfoVec.size(); i++){
		parameterObj = this->parameterInfoVec[i];

		memcpy(binaryParam,curLine + parameterObj->getFirstBitPosition(), parameterObj->getBitCount());
		
		binaryParam[parameterObj->getBitCount()] = '\0';

		(parameterObj->*(parameterObj->binaryToString))(binaryParam,curParams[parameterObj->getParameterID()]);
	}
}

//Takes ATO/ATP stringLabels and prints them to the output file in correct order
//Void return type because output is printed to file
void fileDecodingInfo::printHeader(int numParameters){
	for(int i = 0; i < numParameters; i++){
		fprintf(this->fileInfoStruct->outputFile,"%s\t",this->stringLabels[i]);
	}
	fprintf(this->fileInfoStruct->outputFile,"\n");
}

//Takes curParams data structure obtained from the currently parsed record and prints them to the output file in correct order
//Void return type because output is printed to file
void fileDecodingInfo::printLine(char (*curParams)[MAX_SHORT_STRING_SIZE + 1],int numParameters){
	for(int i = 0; i < numParameters; i++){
		fprintf(this->fileInfoStruct->outputFile,"%s\t",curParams[i]);
	}
	fprintf(this->fileInfoStruct->outputFile,"\n");
}

//If a raw OMAP log had to be decoded, then file pointers must be swapped such that the .txt file can then be parsed
//This is done so that the file parsing functions are consistent if a .txt log had been uploaded
//Void return type because swapping of file pointers is internal
void fileDecodingInfo::swapFilePointers(){
	struct fileInfo* fInfo = this->fileInfoStruct;
	fclose(fInfo->inputFile);
	fclose(fInfo->outputFile);

	fInfo->inputFile = fopen(fInfo->outputFileName,"r");
	sprintf(fInfo->outputFileName,"%s%s%s%s",fInfo->directoryPath,OUTPUT_FILES_DIRECTORY,fInfo->fileName,CSV_SUFFIX);

	if(NULL != nonRecursiveNameCheck(fInfo->outputFileName))
	{
		fInfo->outputFile = fopen(fInfo->outputFileName,"w+");
	}
	else
	{
		printf("Error - failed to make an output file -- exiting now\n");
		exit(0);
	}
}

//Constructor to parameterInfo class
//Takes the current string line of the parameter config file, and breaks it down to a paramter object with the appropriate information
//If a value is -1 for any column, then that value is Not Applicable to that parameter
//This value of -1 and displayType variable determines what function pointer is attributed to each parameterInfo object
parameterInfo::parameterInfo(char* line,char (*enumeratedLabels)[MAX_ATO_VALUES][MAX_SHORT_STRING_SIZE],char (*stringLabels)[MAX_STRING_SIZE]){
	this->parameterID = fast_atoi(strtok(line,"\t"));
	strcpy(stringLabels[this->parameterID],strtok(NULL,"\t"));
	this->unsignedInt = fast_atoi(strtok(NULL,"\t"));
	this->firstBitPosition = fast_atoi(strtok(NULL,"\t"));
	this->bitCount = fast_atoi(strtok(NULL,"\t"));
	this->firstByte = fast_atoi(strtok(NULL,"\t"));
	this->lastByte = fast_atoi(strtok(NULL,"\t"));
	this->quantum = atof(strtok(NULL,"\t"));
	this->offset = fast_atoi(strtok(NULL,"\t"));
	this->displayType = fast_atoi(strtok(NULL,"\t"));
	this->enumeratedLabel = fast_atoi(strtok(NULL,"\t"));
	this->decimalCount = fast_atoi(strtok(NULL,"\t"));

	char* unit = strtok(NULL,"\t");
	if(strcmp(unit,"-1"))
	{
		this->unit = (char*) malloc(MAX_SHORT_STRING_SIZE);
		strcpy(this->unit,unit);
	}
	else
	{
		this->unit = NULL;
	}

	if(this->firstBitPosition > INNER_HEADER_BIT_POS)
	{
		this->firstBitPosition += MAX_INNER_HEADER_BIT_SIZE;
	}
	else
	{
		//do nothing
	}

	//Function declaration based on the above variables
	if( DISPLAY_TYPE_ENUMERATED == this->displayType )
	{
		this->enumeratedLabels = enumeratedLabels;
		this->binaryToString = &parameterInfo::unsignedBinaryToEnumeratedLabelStr;
	}
	else if( DISPLAY_TYPE_HEXADECIMAL == this->displayType )
	{
		this->binaryToString = &parameterInfo::unsignedBinaryToHexadecimalStr;
	}
	else if( DISPLAY_TYPE_BINARY == this->displayType )
	{
		this->binaryToString = &parameterInfo::binaryToBinaryStr;
	}
	else if( DISPLAY_TYPE_DATE == this->displayType )
	{
		this->binaryToString = &parameterInfo::unsignedBinaryToDateStr;
	}
	else if( DISPLAY_TYPE_TIME == this->displayType )
	{
		if(-1 != this->decimalCount)
		{
			this->binaryToString = &parameterInfo::unsignedBinaryToDecimalTimeStr;
		}
		else
		{
			this->binaryToString = &parameterInfo::unsignedBinaryToTimeStr;
		}
	}
	else
	{
		//this->displayType == DISPLAY_TYPE_DECIMAL
		if(-1 != this->decimalCount)
		{
			if( SIGNED_INTEGER == this->unsignedInt )
			{
				this->binaryToString = &parameterInfo::signedBinaryToDecimalPrecisionStr;
			}
			else
			{
				this->binaryToString = &parameterInfo::unsignedBinaryToDecimalPrecisionStr;
			}
		}
		else if( 1 == this->quantum )
		{
			if( SIGNED_INTEGER == this->unsignedInt )
			{
				this->binaryToString = &parameterInfo::signedBinaryToIntegerStr;
			}
			else
			{
				this->binaryToString = &parameterInfo::unsignedBinaryToIntegerStr;
			}
		}
		else
		{
			if( SIGNED_INTEGER == this->unsignedInt )
			{
				this->binaryToString = &parameterInfo::signedBinaryToDecimalStr;
			}
			else
			{
				this->binaryToString = &parameterInfo::unsignedBinaryToDecimalStr;
			}
		}
	}
}

//Destructor of parameterInfo class
//If a unit had been dynamically allocated memory, then this memory shall be freed
parameterInfo::~parameterInfo(){
	if(NULL != this->unit)
	{
		free(this->unit);
		this->unit = NULL;
	}
	else
	{
		//do nothing
	}
}

//ParameterInfo class functions that accept a long long integer, perform the right action for the right displayType, and copies to str for printing purposes to output file

//Prints the enumerated label based on the value provided
void parameterInfo::IntToEnumeratedLabel(unsigned long long value,char* str){
	if(value > MAX_ATO_VALUES || '\0' == *this->enumeratedLabels[this->enumeratedLabel][value])
	{
		sprintf(str,"? key : %llu", value);
	}
	else
	{
		strcpy(str,this->enumeratedLabels[this->enumeratedLabel][value]);
	}
}
//Prints the hexadecimal equivalent of the value provided
void parameterInfo::IntToHexadecimal(unsigned long long value, char* str){
	sprintf(str,"%0*x",this->bitCount >> 2,value);
}
//Prints the integer equivalent of the value provided
void parameterInfo::IntToInteger(long long value, char* str){
	sprintf(str,"%lld",value+this->offset);
}
//Prints the decimal equivalent from the quantum of the value provided
void parameterInfo::IntToDecimal(long long value, char* str){
	sprintf(str,"%.0f",value*this->quantum+this->offset);
}
//Prints the decimal equivalent with decimal points from the quantum of the value provided
void parameterInfo::IntToDecimalPrecision(long long value, char* str){
	sprintf(str,"%0.*f",this->decimalCount,value*this->quantum+this->offset);
}
//Prints the date equivalent of the 64 bit value provided
void parameterInfo::IntToDate(unsigned long long value, char* str){
	epochTimeToDate(value >> 32,str,"%Y/%m/%d");
}
//Prints the time equivalent of the 64 bit value provided
void parameterInfo::IntToTime(unsigned long long value, char* str){
	epochTimeToDate(value >> 32,str,"%H:%M:%S");
}
//Prints the time with millisecond precision of the 64 bit value provided
void parameterInfo::IntToDecimalTime(unsigned long long value, char* str){
	convertToMillisecond(((value & 0xFFC00000) >> 22) - 8,epochTimeToDate(value >> 32,str,"%H:%M:%S"));
}

//Returns a 64 bit unsigned long long integer from a binary string
unsigned long long parameterInfo::unsignedBinaryToDecimal(const char* binaryStr){
	int len = this->bitCount - 1;
	unsigned long long val = 0;
	while(*binaryStr){
		if('1' == *binaryStr++)
		{
			val = val | ( (unsigned long long) 1 << len);
		}
		else
		{
			//do nothing
		}
		len--;
	}
	return val;
}

//Returns a 64 bit signed long long integer from a binary string
long long parameterInfo::signedBinaryToDecimal(const char* binaryStr){
	//Check if MSB is 1
	if('1' == *binaryStr)
	{
		int len = this->bitCount - 1;       
		long long val = 0;
		while(*binaryStr){
			if('0' == *binaryStr++)
			{
				val = val | ( (long long) 1 << len);
			}
			else
			{
				//do nothing
			}
			len--;
		}
		++val *= -1;
		return val;
	}
	else
	{
		return (long long) unsignedBinaryToDecimal(binaryStr);
	}
}

//parameterInfo getter functions 

//Returns parameterId ordering sequence
int parameterInfo::getParameterID(){
	return this->parameterID;
}
//Returns whether parameter is an unsigned or signed type (0 = unsigned, 1 = signed)
int parameterInfo::getUnsignedInt(){
	return this->unsignedInt;
}
//Returns bit position of parameter
int parameterInfo::getFirstBitPosition(){
	return this->firstBitPosition;
}
//Returns bit count of parameter
int parameterInfo::getBitCount(){
	return this->bitCount;
}
//Returns numeric display type
int parameterInfo::getDisplayType(){
	return this->displayType;
}

//parameterInfo class functions that short circuit to working with signed vs unsigned long long values
//This helps save an extra branch condition check in the file parsing stage
void parameterInfo::unsignedBinaryToEnumeratedLabelStr(char* binaryStr,char* str){
	IntToEnumeratedLabel(unsignedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::unsignedBinaryToHexadecimalStr(char* binaryStr,char* str){
	IntToHexadecimal(unsignedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::unsignedBinaryToIntegerStr(char* binaryStr,char* str){
	IntToInteger(unsignedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::signedBinaryToIntegerStr(char* binaryStr,char* str){
	IntToInteger(signedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::unsignedBinaryToDecimalStr(char* binaryStr,char* str){
	IntToDecimal(unsignedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::signedBinaryToDecimalStr(char* binaryStr,char* str){
	IntToDecimal(signedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::unsignedBinaryToDecimalPrecisionStr(char* binaryStr,char* str){
	IntToDecimalPrecision(unsignedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::signedBinaryToDecimalPrecisionStr(char* binaryStr,char* str){
	IntToDecimalPrecision(signedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::binaryToBinaryStr(char* binaryStr,char* str){
	memcpy(str,binaryStr,this->bitCount);
}
void parameterInfo::unsignedBinaryToDateStr(char* binaryStr,char* str){
	IntToDate(unsignedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::unsignedBinaryToTimeStr(char* binaryStr,char* str){
	IntToTime(unsignedBinaryToDecimal(binaryStr),str);
}
void parameterInfo::unsignedBinaryToDecimalTimeStr(char* binaryStr,char* str){
	IntToDecimalTime(unsignedBinaryToDecimal(binaryStr),str);
}
