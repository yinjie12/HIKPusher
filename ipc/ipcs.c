/*******************************
@@Author     : Charles
@@Date       : 2018-07-04
@@Mail       : pu17rui@sina.com
@@Description:          
*******************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "ipcs.h"
#include "rtmp_log.h"  
#include "log.h"

struct _ipc IPCs[IPCS_MAX_NUM];
/*************************************************
@Description: init of the ipc struct
@Input: 
@Output: 
@Return: 0 - success
@Others: 
*************************************************/
int IPCS_Init(void)
{
	for (int i = 0; i < IPCS_MAX_NUM; ++i)
	{
		IPCs[i].login_id 			= -1;
		IPCs[i].stream_handle 		= -1;
		IPCs[i].preview_session_id 	= -1;

		short co_id 	= i / 16 % 16;
		short sta_id  	= i % 16;
		sprintf(IPCs[i].dev_id, "IPCS00000%x0%x", co_id, sta_id);
		// printf("dev_id:%s \n", IPCs[i].dev_id);

		memset(&(IPCs[i].last_req_time), 0, sizeof(IPCs[i].last_req_time));
		IPCs[i].push_state 				= IPCS_NOT_PUSHING_STREAM;
		IPCs[i].online_state 			= IPCS_OFFLINE;

		IPCs[i].rtmp = NULL;
	    IPCs[i].pespack_buf = NULL;
	    IPCs[i].pespack_buf_len = 0;
	    IPCs[i].h264pack_buf = NULL;
	    IPCs[i].h264pack_buf_len = 0;
	    
		memset(&(IPCs[i].metaData), 0, sizeof(IPCs[i].metaData));
		IPCs[i].tick = 0;
		IPCs[i].tick_gap = 0; 
	}

	return 0;
}
/*************************************************
@Description: init of one ipc
@Input: 
@Output: 
@Return: 0 - success
@Others: 
*************************************************/
int IPCS_PushInit(struct _ipc *ipc)
{
	RTMP *rtmp = NULL;
	rtmp = RTMP_Alloc();  
    RTMP_Init(rtmp);  
    //set connection timeout,default 30s  
    rtmp->Link.timeout = RTMP_CONNECTION_TIMEOUT;                        
    char url_tmp[50] = {0};
    int posi = sprintf(url_tmp, "%s", RTMP_URL);   
    sprintf(url_tmp + posi, "%s", ipc->dev_id);//printf("%s\n", url_tmp);                    
    if(!RTMP_SetupURL(rtmp, (char*)url_tmp))  
    {  
       // RTMP_Log(RTMP_LOGERROR, "SetupURL Err\n"); 
       LOG_ERROR(ERR_RTMP_SET_URL, "RTMP SetupURL Err\n");
       RTMP_Free(rtmp);  
       return 1;  
    }  
    //if unable,the AMF command would be 'play' instead of 'publish'  
    RTMP_EnableWrite(rtmp);       

    if (!RTMP_Connect(rtmp, NULL)){  
       // RTMP_Log(RTMP_LOGERROR, "Connect Err\n");  
       LOG_ERROR(ERR_RTMP_CONNECT, "RTMP Connect Err\n");
       RTMP_Free(rtmp);  
       return 2;  
    }  

    if (!RTMP_ConnectStream(rtmp, 0)){  
       LOG_ERROR(ERR_RTMP_CONNECT_STREAM, "RTMP ConnectStream Err\n");
       // RTMP_Log(RTMP_LOGERROR, "ConnectStream Err\n");  
       RTMP_Close(rtmp);  
       RTMP_Free(rtmp);  
       return 3;  
    }
    /*this statement must be after the init of rtmp!!*/
    ipc->rtmp = rtmp;

    ipc->pespack_buf = (char*)malloc(PESPACK_BUF_MAX_SIZE);
    for (int i = 0; i < PESPACK_BUF_MAX_SIZE; ++i)
    	ipc->pespack_buf[i] = 0;
    ipc->pespack_buf_len = 0;
    ipc->pespack_left_len = 0;
    ipc->h264pack_buf = (char*)malloc(H264PACK_BUF_MAX_SIZE);
    for (int i = 0; i < H264PACK_BUF_MAX_SIZE; ++i)
    	ipc->h264pack_buf[i] = 0;
    ipc->h264pack_buf_len = 0;

	//in case...
	if (ipc->metaData.Sps != NULL)
	{
		free(ipc->metaData.Sps);
		ipc->metaData.Sps = NULL;
	}
	if (ipc->metaData.Pps != NULL)
	{
		free(ipc->metaData.Pps);
		ipc->metaData.Pps = NULL;
	}
	memset(&(ipc->metaData), 0, sizeof(ipc->metaData));
	ipc->tick = 0;
	ipc->tick_gap = 0; 

    return 0;
}
/*************************************************
@Description: init of the ipc struct
@Input: 
@Output: 
@Return: 0 - success
@Others: 
*************************************************/
int IPCS_PushFree(struct _ipc *ipc)
{
	if (ipc->rtmp != NULL)
    {  
       RTMP_Close(ipc->rtmp);          
       RTMP_Free(ipc->rtmp);   
       ipc->rtmp = NULL;  
    }
    ipc->pespack_buf_len = 0;
    ipc->pespack_left_len = 0;
    if (ipc->pespack_buf != NULL)
    {
    	free(ipc->pespack_buf);
    	ipc->pespack_buf = NULL;
    }
    ipc->h264pack_buf_len = 0;
    if (ipc->h264pack_buf != NULL)
    {
    	free(ipc->h264pack_buf);
    	ipc->h264pack_buf = NULL;
    }
	//must free first!
	if (ipc->metaData.Sps != NULL)
	{
		free(ipc->metaData.Sps);
		ipc->metaData.Sps = NULL;
	}
	if (ipc->metaData.Pps != NULL)
	{
		free(ipc->metaData.Pps);
		ipc->metaData.Pps = NULL;
	}
	memset(&(ipc->metaData), 0, sizeof(ipc->metaData));
	ipc->tick = 0;
	ipc->tick_gap = 0; 

    return 0;
}
/*************************************************
@Description: Get Devid(int)
@Input: req_serv's recv buf except "start"; buf's length
@Output: 
@Return: 
@Others: reg_pack : I P C S 00 00 0X 0X
*************************************************/
unsigned short IPCS_GetInt_Devid(unsigned char *msgs, int len)
{
	// char str_user_id[2] = {0};//must be 2 cause the '\0'
	// char str_loc_id[2]  = {0};
	// char str_sta_id[2]  = {0};

	// if (len < 12)
	// 	return -1;
	// memcpy(str_user_id, msgs + 7, 1);
	// memcpy(str_loc_id, msgs + 9, 1);
	// memcpy(str_sta_id, msgs + 11, 1);
	// // printf("usr:%s, loc:%s, sta:%s\n", str_user_id, str_loc_id, str_sta_id);

	// short user_id = strtol(str_user_id, NULL, 16);
	// short loc_id  = strtol(str_loc_id, NULL, 16);
	// short sta_id  = strtol(str_sta_id, NULL, 16);
	// // printf("usr:0x%02x, loc:0x%02x, sta:0x%02x\n", user_id, loc_id, sta_id);

	// unsigned short int_dev_id = (user_id << 8) + (loc_id << 4) + sta_id;
	// // printf("int_dev_id:%d\n", int_dev_id);

	char str_co_id[2] = {0};//must be 2 cause the '\0'
	char str_sta_id[2]  = {0};

	if (len < 12)
		return -1;
	memcpy(str_co_id, msgs + 9, 1);
	memcpy(str_sta_id, msgs + 11, 1);
	// printf("usr:%s, sta:%s\n", str_co_id, str_sta_id);

	short co_id  = strtol(str_co_id, NULL, 16);
	short sta_id = strtol(str_sta_id, NULL, 16);
	// printf("usr:0x%02x, sta:0x%02x\n", co_id, sta_id);

	unsigned short int_dev_id = (co_id << 4) + sta_id;
	// printf("int_dev_id:%d\n", int_dev_id);

	return int_dev_id;
}