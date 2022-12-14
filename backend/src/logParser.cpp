#include "globals.h"
#include "logParser.h"
#include "logDecoderClass.h"
#include "helperFunctions.h"

//Constructor for fileParsingInfo class
fileParsingInfo::fileParsingInfo(struct fileInfo* fileInfoStruct, int logType,time_t startTime,time_t endTime,short *argN,int numArguments)
	: fileDecodingInfo{ fileInfoStruct, logType } , argN( argN ), numArguments( numArguments )
	{
	this->UTC = determineESTorEDT(startTime);
	this->startTime = startTime + (SECS_PER_HOUR * this->UTC);
	this->endTime = endTime + (SECS_PER_HOUR * this->UTC);
	//Allocate space for current record struct and nullify previous record
	this->curRecord = NULL;
	this->prevRecord = NULL;
}

//Destructor for fileParsingInfo class
//Closes all input and output files
fileParsingInfo::~fileParsingInfo(){
	if(NULL != this->prevRecord)
	{
		free(this->prevRecord);
	}
	else
	{
		//do nothing
	}
	fclose(this->fileInfoStruct->inputFile);
	fclose(this->fileInfoStruct->outputFile);
}

//Takes a line from the DLU log & finds the necessary arguments
//argN holds the numeric argument -- location of where the token is delimeted by \t
//Void return type because output is copied to the logArgs array
void fileParsingInfo::tokenizeLine(){

	int tokenCount = ARG_NUM_AFTER_DATE_TIME;	
	char* tokenStr = strtok(NULL,"\t");
	int argCount = 0;
	//tokenizes the line and completes logArgs
	while(argCount < this->numArguments || NULL != tokenStr){
		if(this->argN[argCount] == tokenCount)
		{
			strcpy(this->curRecord->logArgs[argCount],tokenStr);
			argCount++;
		}
		else
		{
			//do nothing
		}
		tokenCount++;
		tokenStr = strtok(NULL,"\t");
	}
}

//Writes the output parameter string labels in the correct order
void fileParsingInfo::writeHeaderLine(){

	for(int i = 0;i < this->numArguments; i++){
		fprintf(this->fileInfoStruct->outputFile,",%s",this->stringLabels[this->argN[i] - 1]);
	}
}

//Static function that can be used by any other function
//Necessary for concatenating files
//Writes the output parameter string labels in the correct order
void fileParsingInfo::writeHeader(FILE* outputFile,int argC,char (*stringLabels)[MAX_STRING_SIZE],short* argN){
	for(int i = 0;i <argC; i++){
		fprintf(outputFile,",%s",stringLabels[argN[i] - 1]);
	}
}

//Main function that writes a row to the fileOutput if the preceding row exhibited any differences
//recordArray holds the records that correspond to the first second of the fileInput
//Necessary for accurate timestamping -- The first line from the DLU file is always printed
//Afterwards, the current line is compared to the previous line for any differences - if so, then it is printed to the .csv file
//Void return type because output is printed to internal output files
void fileParsingInfo::writeChangingRecords(){
	char* line = (char*) malloc(MAX_LINE_SIZE);
	time_t firstSecond = 0;
	int lineCount = 0;	
	int numInitFiles = 0;
	int incCounter = 0;

	struct recordInfo* recordArray[MAX_INIT_TIMESTAMPS];
	//Print First Record
	while(fgets(line,MAX_LINE_SIZE ,this->fileInfoStruct->inputFile) != NULL){	
		//Header line of log
		if( 0 == lineCount )
		{
			fprintf(this->fileInfoStruct->outputFile,"Timestamp");
			writeHeaderLine();			
			fprintf(this->fileInfoStruct->outputFile,"\n");
			lineCount++;
		}
		else
		{
			//Epoch time based on date and time fields
			char* yearDate = strtok(line,"\t");
			char* hourDate = strtok(NULL,"\t");
			time_t curTime = determineEpochTime(yearDate,hourDate);
			this->curRecord = (struct recordInfo*)malloc(sizeof(struct recordInfo));
			this->curRecord->epochTime = curTime;

			//Case where the specified startTime is not yet reached
			if(difftime(curTime,this->startTime) < 0)
			{
				free(this->curRecord);
			}
			else
			{
				//Case where the specified endTime is reached before the EOF
				if(difftime(curTime,this->endTime) > 0)
				{
					free(this->curRecord);
				}
				else
				{

					if(!firstSecond){
						firstSecond = curTime;
					}
					else{
						//do nothing
					}
					//Fills in logArgs
					tokenizeLine();

					//The first second of the log
					if( 0 == difftime(curTime,firstSecond) )
					{
						this->startTime = curTime;
						recordArray[numInitFiles] = this->curRecord; 
						recordArray[numInitFiles+1] = NULL; 
						numInitFiles++;
					}
					else
					{
						//write the first time block, then frees & nullifies so that it is only run once
						if( NULL != recordArray[0] )
						{
							writeFirstRecord(recordArray,numInitFiles);
						}
						else
						{
							//do nothing
						}
						//General Line Case - previous record gets checked with current record
						//incCounter is used to increment the second fraction accurately
						if( 0  != difftime(this->curRecord->epochTime,this->prevRecord->epochTime) )
						{
							incCounter = 0;
						}
						else
						{
							incCounter++;
							if(this->fileInfoStruct->logType == ATP_NUM)
							{
								incCounter++;
							}
							else
							{
								//do nothing
							}
						}
						this->curRecord->secondFraction = incCounter;

						//check difference between cur and prev - if difference; write to .csv file
						if(changeInRecords())
						{
							writeRecordInfo();
						}
						else
						{
							//do nothing
						}
						free(this->prevRecord);
						this->prevRecord = this->curRecord;
					}
					lineCount++;
				}
			}
		}
	}
	free(line);
}

//Iterates over recordArray, and updates the fraction second based on how many lines exist in the first second
//Writes the first record, all other records are only written if there is change between records
//Frees all other records but the last one for reference to the general case
void fileParsingInfo::writeFirstRecord(struct recordInfo* recordArray[],int numInitFiles){
	struct recordInfo* curHolder = this->curRecord;
	for(struct recordInfo** temp = recordArray;*temp != NULL;temp++){
		this->curRecord = *temp;
		numInitFiles = updateSecondFraction(numInitFiles);

		if(*temp == recordArray[0] || changeInRecords()) 
		{
			writeRecordInfo();
		}
		else
		{
			//do nothing
		}
		this->prevRecord = *temp;
	}
	for(struct recordInfo** temp = recordArray;*temp != this->prevRecord;temp++){
		free(*temp);	
		*temp = NULL;
	}
	this->curRecord = curHolder;
}

//Iterates over the array for argument values & compares each value by string index-wise
//Short circuits and returns true if there is any character-based difference -- false if otherwise
bool fileParsingInfo::changeInRecords(){
	//Any logs exhibit change
	for(int i = 0; i < this->numArguments; i++){
		if(strcmp(this->prevRecord->logArgs[i],this->curRecord->logArgs[i])) 
		{
			return true;
		}
		else
		{
			//do nothing
		}
	}

	return false;
}

//uses the 'strftime()' function in 'time.h' to convert an epoch time to a string
//Each record struct has an epoch time that must be converted to a string for human-reading
//Returns the new string date converetd from the epoch time that was adjusted based on the timezone
char* fileParsingInfo::convertEpochToString(char UTCTimestamp[]){
	struct tm* curTMstruct = localtime(&(this->curRecord->epochTime));
	curTMstruct->tm_hour -= this->UTC;
	curTMstruct->tm_isdst = -1;
	mktime(curTMstruct);
	//format is YYYY-MM-DD HH:MI:SS
	if( 0 != strftime(UTCTimestamp,MAX_STRING_SIZE,"%Y-%m-%d %H:%M:%S",curTMstruct) )
	{
		return UTCTimestamp;
	}
	else {
		printf("Error: Converting Record %p Of Time %d to String was Unsuccessful\n",this->curRecord,this->curRecord->epochTime);
		printf("%s \n",UTCTimestamp);
		exit(EXIT_FAILURE);
	}
}

//Writes the timestamp & the arguments of the curRecord struct
//Only called if there is change with the previous record, or if it is the first record in the log file
void fileParsingInfo::writeRecordInfo(){
	char UTCTimestamp[MAX_STRING_SIZE];
	fprintf(this->fileInfoStruct->outputFile,"%s.%d",convertEpochToString(UTCTimestamp),this->curRecord->secondFraction);

	for(int i = 0; i < this->numArguments;i++){
		fprintf(this->fileInfoStruct->outputFile,",%s",this->curRecord->logArgs[i]);
	}
	fprintf(this->fileInfoStruct->outputFile,"\n");
}

//updates the fraction based on the block containing the first second
//Returns the number of how many records are LEFT to parse
//ATO Logs Inc by 0.1 seconds 
//ATP Logs Inc by 0.2 seconds
int fileParsingInfo::updateSecondFraction(int numInitFiles){
	int logInc = 1;
	if( ATP_NUM == this->fileInfoStruct->logType )
	{
		logInc = 2;
	}
	else
	{
		//do nothing
	}
	this->curRecord->secondFraction = 10 - (logInc * numInitFiles);
	return --numInitFiles;
}

//Function that loops over fileArray to write all changing records of each file
//Each input will have its own separate output file with the changing records
//These will then get parsed to be concatenated (if multiple)
void fileParsingInfo::parseLogFile(){

	if( NULL == this->fileInfoStruct->inputFile )
	{
		printf("Unable to open Input files\n");
		exit(EXIT_FAILURE);
	}
	else
	{
		//do nothing
	}

	if( NULL == this->fileInfoStruct->outputFile )
	{
		printf("Unable to open Output files\n");
		exit(EXIT_FAILURE);
	}
	else
	{
		//do nothing
	}

	writeChangingRecords();
}

//fileParsingInfo getter function
//returns number of arguments that was inputted by the user
int fileParsingInfo::getNumArguments(){
	return this->numArguments;
}

//Checks if input files are part of different cores
//returns the core number if the cores are unanimous
//returns 0 to signify some files are of core 1 & some are of core 2
int checkMultCores(std::vector<fileParsingInfo*> &fileVec){
	for(int i = 1; i <fileVec.size(); i++){
		if(fileVec[i]->getCoreType() != fileVec[i-1]->getCoreType())
		{
			return CORE_MULT;
		}
		else
		{
			//do nothing
		}
	}
	return fileVec[0]->getCoreType();
}

//Prints 'times' many commas to the output
//Necessary for spacing in .csv files between different cores
void repeatCommas(FILE* outputFile,int times){
	for(int i = 0; i < times; i++){
		fprintf(outputFile,",");
	}
}

//Prints out the core number columns to the .csv
//Core 1 & Core 2 OR Core #
void writeCoreHeader(FILE* outputFile, int multCores, int argC){
	fprintf(outputFile,",Core %d", (!multCores ? CORE_ONE : multCores));
	repeatCommas(outputFile,(argC - 1));

	if(!multCores)
	{
		fprintf(outputFile,",Core %d", CORE_TWO);
		repeatCommas(outputFile,(argC - 1));
	}
	else
	{
		//do nothing
	}
	fprintf(outputFile,"\n");
}

//Loops over files in fileArray & prints their outputs to outputFile .csv
//Spacing adjusted if files deal with multiple cores 
void concatFiles(FILE* outputFile,std::vector<fileParsingInfo*> &fileVec){
	int multCores = checkMultCores(fileVec);
	int argC = fileVec[0]->getNumArguments();
	char filePath[MAX_STRING_SIZE];

	for(int i = 0; i < fileVec.size();i++){
		char line[MAX_LINE_SIZE];
		int lineCount = 0;

		rewind(fileVec[i]->getOutputFile());
		while(NULL != fgets(line, sizeof(line),fileVec[i]->getOutputFile())){
			if(!lineCount)
			{
				lineCount++;
			}
			else
			{
				if( 1 == lineCount )
				{
					fprintf(outputFile,"%s,",fileVec[i]->getFileName());
				}	
				else
				{
					fprintf(outputFile,",");
				}

				//Dealing with multiple cores + file is part of core 2 -> spacing must be adjusted with commas
				if(!multCores && (CORE_TWO == fileVec[i]->getCoreType()))
				{
					//Print the timestamp -- the first 21 chars
					fprintf(outputFile,"%.21s",line);
					//Print empty commas
					repeatCommas(outputFile,argC);
					//Print the rest of the data
					fprintf(outputFile,"%s",line+CHARS_AFTER_TIMESTAMP);

				}
				else
				{
					line[strlen(line)-1]='\0';
					fprintf(outputFile,"%s",line);
					if(!multCores)
					{
						repeatCommas(outputFile,argC);
					}
					else
					{
						//do nothing
					}
					fprintf(outputFile,"\n");
				}
				lineCount++;
			}
		}
		remove(fileVec[i]->getOutputFileName());
	}
}

//Takes the outputFileName & creates a file that will contain all other input files
//Adjusts for spacing, core specificity, file name, and includes all changing records of all files
void makeCombinedOutput(char* parentDir,char* outputFileName,std::vector<fileParsingInfo*> &fileVec){
	
	char outputFilePath[MAX_STRING_SIZE];
	FILE* outputFile;
	if('\0' != *outputFileName)
	{
		sprintf(outputFilePath,"%s%s%s%s",parentDir,OUTPUT_FILES_DIRECTORY,outputFileName,CSV_SUFFIX);
	}
	else
	{
		sprintf(outputFilePath,"%s%s%s%s",parentDir,OUTPUT_FILES_DIRECTORY,GENERAL_OUTPUT_NAME,CSV_SUFFIX);
	}
	if(NULL != nonRecursiveNameCheck(outputFilePath))
	{
		outputFile = fopen(outputFilePath,"w");
		if( NULL == outputFile )
		{
			printf("Error -- Could not open Output file -- Terminating Program \n");
			exit(0);
		}
		else
		{
			//do nothing
		}
	}
	else
	{
		exit(0);
	}

	printf("File %s Has Been Created in ./outputFiles/ \n",outputFilePath+strlen(parentDir)+strlen(OUTPUT_FILES_DIRECTORY));
	
	int multCores = checkMultCores(fileVec);
	fprintf(outputFile,"%s,",outputFilePath+strlen(parentDir)+strlen(OUTPUT_FILES_DIRECTORY));
	writeCoreHeader(outputFile,multCores,fileVec[0]->getNumArguments());

	fprintf(outputFile,",Timestamp");
	fileParsingInfo::writeHeader(outputFile,fileVec[0]->getNumArguments(),(fileVec[0]->getLogType() == ATO_NUM) ? ATO_StringLabels : ATP_StringLabels,argN);
	if(!multCores)
	{
		fileParsingInfo::writeHeader(outputFile,fileVec[0]->getNumArguments(),(fileVec[0]->getLogType() == ATO_NUM) ? ATO_StringLabels : ATP_StringLabels,argN);
	}
	else
	{
		//do nothing
	}
	fprintf(outputFile,"\n");

	concatFiles(outputFile,fileVec);

	fclose(outputFile);
}
