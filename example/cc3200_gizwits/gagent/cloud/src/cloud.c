#include "gagent.h"
#include "http.h"
#include "mqttxpg.h"
#include "cloud.h"
#include "3rdcloud.h"
#include "utils.h"
/*
return 0 OTA SUCCESS
*/
int32 GAgent_MCUOTAByUrl( pgcontext pgc,int8 *downloadUrl )
{
    //TODO.
    int32 ret = 0;
    int32 http_socketid = -1;
    uint8 OTA_IP[32]={0};
    int8 *url = NULL;
    int8 *host = NULL;
    int8 dnsTime=0;
    int8 ota_time=0;


    if( RET_FAILED == Http_GetHost( downloadUrl,&host,&url ) )
    {
        return RET_FAILED;
    }
   
     CC3200_DNSTaskDelete();
    while( 1 )
    {
        if( dnsTime>=5 )
        {   
            break;
        }
        
        if( 0==GAgent_GetHostByName( host, OTA_IP ) )
        {
            break;
        }
        sleep(1);
        dnsTime++;
        
    }
    if( OTA_IP!=NULL )
    {
        http_socketid = Cloud_InitSocket( http_socketid, OTA_IP, 80, 0 );
    }
    GAgent_Printf( GAGENT_DEBUG,"OTA_socket:%d",http_socketid );
    GAgent_Printf( GAGENT_DEBUG, "downloadUrl BUF :\n%s\n----------------------\nlen =%d\n",downloadUrl,strlen(downloadUrl) );
    GAgent_Printf( GAGENT_DEBUG,"host:%s\n",host );
    GAgent_Printf( GAGENT_DEBUG, "ip:%s\n",OTA_IP );
    if( http_socketid<0 )
    {
        GAgent_Printf( GAGENT_DEBUG,"http_socketid:%d ",http_socketid);
        return RET_FAILED;
    }
    
    ret = Http_ReqGetFirmware( url,host,http_socketid );
    if( RET_SUCCESS == ret )
    {
        ret = Http_ResGetFirmware( pgc,http_socketid );
        close(http_socketid);
        return ret;
    }
    else
    {
        GAgent_Printf(GAGENT_WARNING,"req get Firmware failed!\n");
        close(http_socketid);
        return RET_FAILED;
    }
}
uint32 GAgent_ReqServerTime(pgcontext pgc)
{
    uint32 ret; 
    if((pgc->rtinfo.GAgentStatus&WIFI_STATION_CONNECTED) !=  WIFI_STATION_CONNECTED)
    {
        return (uint32)RET_FAILED;
    }
    ret = Cloud_ReqProvision( pgc );
    pgc->rtinfo.waninfo.send2HttpLastTime = GAgent_GetDevTime_S(); 
    if(0 != ret)
    {   
        GAgent_Printf(GAGENT_WARNING,"Provision fail!\n");
        return (uint32)RET_FAILED;
    }
    return (uint32)RET_SUCCESS;
}
uint32 GAgent_Get_Gserver_Time( uint32 *clock, uint8 *Http_recevieBuf, int32 respondCode )
{
    int8 *p_start = NULL;
    int8 *p_end =NULL;   
    int8 stime[20]={0};
    uint32 time;
    if( 200 != respondCode )
    {        
        return (uint32)RET_FAILED;   
    }   
    p_start = strstr((char *)Http_recevieBuf, "server_ts=");
    if( p_start==NULL ) 
        return (uint32)RET_FAILED;   
    p_start = p_start+strlen("server_ts=");
    p_end = strstr( p_start,"&" ); 
    if( p_end == NULL )
    {
        p_end = strstr( p_start,"\r" ); 
    }    
    memcpy(stime,p_start,( p_end-p_start));
    time = atoi(stime);
    *clock = time;
    return (uint32)RET_SUCCESS;
}
/****************************************************************
        FunctionName    :   GAgent_Cloud_SendData
        Description     :   send buf data to M2M server.
        return          :   0-ok 
                            other fail.
        Add by Alex.lin     --2015-03-17
****************************************************************/
uint32 GAgent_Cloud_SendData( pgcontext pgc,ppacket pbuf,int32 buflen )
{
    int8 ret = 0;
    uint16 cmd;
    
    stCloudAttrs_t *client = &pgc->rtinfo.stChannelAttrs.cloudClient;
    cmd = client->cmd;
    if( isPacketTypeSet( pbuf->type,CLOUD_DATA_OUT ) == 1)
    {
         pbuf->type = SetPacketType( pbuf->type,CLOUD_DATA_OUT,0 );
        ret = MQTT_SendData( pgc, pgc->gc.DID, pbuf,buflen );
        GAgent_Printf(GAGENT_INFO,"Send date to cloud :len =%d ,ret =%d",buflen,ret );
        
    }
    return ret;
}
/****************************************************************
        Function        :   Cloud_InitSocket
        Description     :   init socket connect to server.
        iSocketId       :   socketid.
        p_szServerIPAddr:   server ip address like "192.168.1.1"
        port            :   server socket port.
        flag            :   =0 init socket in no block.
                            =1 init socket in block.
        return          :   >0 the cloud socket id 
                            <=0 fail.
****************************************************************/
int32 Cloud_InitSocket( int32 iSocketId,int8 *p_szServerIPAddr,int32 port,int8 flag )
{
    int32 ret=0;
    int32 tempSocketId=0;
    ret = strlen( p_szServerIPAddr );
    
    if( ret<=0 || ret> 17 )
        return RET_FAILED;

    GAgent_Printf(GAGENT_DEBUG,"socket connect cloud ip:%s .",p_szServerIPAddr);
    if( iSocketId > 0 )
    {
        GAgent_Printf(GAGENT_DEBUG, "Cloud socket need to close SocketID:[%d]", iSocketId );
        close( iSocketId );
        iSocketId = INVALID_SOCKET;
    }

    if( (iSocketId = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))<=0)
    {
        GAgent_Printf(GAGENT_ERROR," Cloud socket init fail");
        return RET_FAILED;
    }
    tempSocketId = iSocketId;
    GAgent_Printf(GAGENT_DEBUG, "New cloud socketID [%d], port=%d",iSocketId, port);

    //Gagent_setsocketnonblock(iSocketId); //Nik.chen add here problem?
    //Gagent_setsocketrectime(iSocketId);
    
    iSocketId = GAgent_connect( iSocketId, port, p_szServerIPAddr,flag );

    if ( iSocketId <0 )
    {
        close( tempSocketId );
        iSocketId=INVALID_SOCKET;
        GAgent_Printf(GAGENT_ERROR, "Cloud socket connect fail with:%d", ret);
        return -3;
    }
    return tempSocketId;
}
/****************************************************************
        Function    :   Cloud_ReqRegister
        description :   sent register data to cloud
        Input       :   NULL;
        return      :   0-send register data ok.
                        other fail.
        add by Alex.lin     --2015-03-02
****************************************************************/
uint32 Cloud_ReqRegister( pgcontext pgc )
{
    uint32 socket = 0;
    int8 ret = 0;

    pgcontext pGlobalVar=NULL;
    pgconfig pConfigData=NULL;

    pGlobalVar=pgc;
    pConfigData = &(pgc->gc);
    pGlobalVar->rtinfo.waninfo.http_socketid = Cloud_InitSocket( pGlobalVar->rtinfo.waninfo.http_socketid,pConfigData->GServer_ip,80,0 );
    socket = pGlobalVar->rtinfo.waninfo.http_socketid ;
    
    if( socket<0 )
    {
        return (uint32)RET_FAILED;
    }

    #if 0 //Nik.chen for test
    #define PRODUCT_KEY   "8bef2268616a4a2990a6761a35830797" 
    memcpy(pGlobalVar->mcu.product_key, PRODUCT_KEY, strlen(PRODUCT_KEY)); 
    #endif
   
     GAgent_Printf(GAGENT_DEBUG,"Cloud_ReqRegister, http socketid=%d", socket);
    ret = Http_POST( socket, HTTP_SERVER,pConfigData->wifipasscode,(char *)pGlobalVar->minfo.szmac,
                     (char *)pGlobalVar->mcu.product_key );
    
    if( RET_SUCCESS!=ret )
    {
        return (uint32)RET_FAILED;
    }
    else
    {
        return (uint32)RET_SUCCESS;
    }
}
/* 
    will get the device id.
*/
int32 Cloud_ResRegister( uint8 *cloudConfiRxbuf,int32 buflen,int8 *pDID,int32 respondCode )
{
    int32 ret=0;
    
    if( 201 != respondCode)
        return RET_FAILED;
    ret = Http_Response_DID( cloudConfiRxbuf,pDID );
    if( RET_SUCCESS==ret )
    {
        return RET_SUCCESS;
    }
    else 
        return RET_FAILED;
}

uint32 Cloud_ReqGetSoftver( pgcontext pgc,enum OTATYPE_T type )
{
    int32 socket = 0;
    //int8 ret = 0;
    int8 *hver, *sver;
    pgcontext pGlobalVar=NULL;
    pgconfig pConfigData=NULL;
    
    pGlobalVar=pgc;
    pConfigData = &(pgc->gc);
    pGlobalVar->rtinfo.waninfo.http_socketid = Cloud_InitSocket( pGlobalVar->rtinfo.waninfo.http_socketid,pConfigData->GServer_ip,80,0 );
    socket = pGlobalVar->rtinfo.waninfo.http_socketid;
    
    if( socket<=0 )
        return (uint32)RET_FAILED;
    
    GAgent_Printf(GAGENT_DEBUG, "http socket connect OK with:%d", socket);
    switch( type )
    {
        case OTATYPE_WIFI:
                hver = WIFI_HARDVER;
                sver = WIFI_SOFTVAR;
            break;
        case OTATYPE_MCU:
                hver = (char *)(pGlobalVar->mcu.hard_ver);
                sver = (char *)pGlobalVar->mcu.soft_ver;
            break;
        default:
            GAgent_Printf( GAGENT_WARNING,"GAgent OTA type is invalid! ");
            return (uint32)RET_FAILED;
    }
    
    CheckFirmwareUpgrade( HTTP_SERVER,pConfigData->DID,type,pConfigData->wifipasscode,hver,sver,socket );                  
    return (uint32)RET_SUCCESS;
    
}

/****************************************************************
*       FunctionName    :   Cloud_ResGetFid.
*       Description     :   get firmwarm download url and firmwarm version.
*       buf             :   data form cloud after req fid.
*       download_url    :   new firmwarm download url
*       fwver           :   new firmwarm version.
*       respondCode     :   http respond code.
*       reutn           :   0 success other error.
*       Add by Alex.lin   --2015-03-03
****************************************************************/
int32 Cloud_ResGetSoftver( int8 *downloadurl, int8 *fwver, uint8 *cloudConfiRxbuf,int32 respondCode )
{
    int32 ret=0;
    
    if( 200 != respondCode )
        return RET_FAILED;
    ret = Http_GetSoftver_Url( downloadurl, fwver, cloudConfiRxbuf );
    if( RET_SUCCESS != ret )
    {
        return RET_FAILED;
    }
    return RET_SUCCESS;
}

/****************************************************************
*       FunctionName    :   Cloud_ReqProvision
*       Description     :   send provision req to host.
*       host            :   GServer host,like "api.gizwits.com"
*       return          :   0 success other error.
*       Add by Alex.lin   --2015-03-03
****************************************************************/
uint32 Cloud_ReqProvision( pgcontext pgc )
{
    int32 socket = 0;
    int8 ret = 0;
    pgcontext pGlobalVar=NULL;
    pgconfig pConfigData=NULL;
    
    pGlobalVar=pgc;
    pConfigData = &(pgc->gc);
    
    pGlobalVar->rtinfo.waninfo.http_socketid = Cloud_InitSocket( pGlobalVar->rtinfo.waninfo.http_socketid,pConfigData->GServer_ip,80,0 );
    socket = pGlobalVar->rtinfo.waninfo.http_socketid;
    if( socket<=0 )
        return (uint32)RET_FAILED;

    GAgent_Printf(GAGENT_DEBUG,"Cloud_ReqProvision, http socketid=%d", socket);
    
    ret = Http_GET( HTTP_SERVER,pConfigData->DID,socket );
    return ret;
}
/****************************************************************
*       FunctionName    :   Cloud_ResProvision.
*       Description     :   data form server after provision.
*       szm2mhost       :   m2m server like: "m2m.gizwits.com"
*       port            :   m2m port .
*       respondCode     :   http respond code.
*       return          :   0 success other fail.
****************************************************************/
uint32 Cloud_ResProvision( int8 *szdomain,int32 *port,uint8 *cloudConfiRxbuf,int32 respondCode )
{
    int32 ret = 0;
    if( 200 != respondCode )
        return (uint32)RET_FAILED;
    ret = Http_getdomain_port( cloudConfiRxbuf,szdomain,port );
    return ret;
}
/****************************************************************
*       FunctionName    :   Cloud_isNeedOTA
*       sFV             :   soft version
*       return          :   1 do not need to OTA
*                           0 need to OTA.
*       Add by Alex.lin   --2015-03-03
****************************************************************/
uint32 Cloud_isNeedOTA( pgcontext pgc, int type, int8 *sFV )
{
    int32 result=0;
    switch( type )
    {
        case OTATYPE_WIFI:
    result = strcmp( WIFI_SOFTVAR,sFV );
            if( result < 0 )
                return RET_SUCCESS;
            break;
        case OTATYPE_MCU:
            result = strcmp((char *)pgc->mcu.soft_ver,sFV );
            if( result < 0 )
                return RET_SUCCESS;
            break;
        default:
            return (uint32)RET_FAILED;
    }
    return (uint32)RET_FAILED;   
}
/****************************************************************
        Function    :   Cloud_ReqConnect
        Description :   send req m2m connect packet.
        username    :   username.
        password    :   username.
        return      :   0: send req connect packet ok
                        other req connect fail.
        Add by Alex.lin     --2015-03-09
****************************************************************/
uint32 Cloud_ReqConnect( pgcontext pgc,const int8 *username,const int8 *password )
{
    int8 ret = 0;
    int32 socket = 0;
    pgcontext pGlobalVar=NULL;
    pgconfig pConfigData=NULL;
    int32 nameLen=0,passwordLen=0;

    pGlobalVar=pgc;
    pConfigData = &(pgc->gc);

    nameLen = strlen( username );
    passwordLen = strlen( password );
    
    if( nameLen<=0 || nameLen>22 ) /* invalid name */
    {
        GAgent_Printf( GAGENT_WARNING," can't req to connect to m2m invalid name length !");
        return 1;
    }
    if( passwordLen<=0 || passwordLen>16 )/* invalid password */
    {
        GAgent_Printf( GAGENT_WARNING," can't req to connect to m2m invalid password length !");
        return 1;
    }
    GAgent_Printf( GAGENT_INFO,"Connect to server domain:%s port:%d",pGlobalVar->minfo.m2m_SERVER,pGlobalVar->minfo.m2m_Port );
    

    pGlobalVar->rtinfo.waninfo.m2m_socketid = Cloud_InitSocket( pGlobalVar->rtinfo.waninfo.m2m_socketid ,pConfigData->m2m_ip ,
                                                         pGlobalVar->minfo.m2m_Port,0 );
    socket = pGlobalVar->rtinfo.waninfo.m2m_socketid;

    if( socket<0 )
    {
        GAgent_Printf(GAGENT_WARNING,"m2m socket :%d",socket);
        return (uint32)RET_FAILED;
    }
    GAgent_Printf(GAGENT_DEBUG,"Cloud_InitSocket OK!, M2M socketid=%d", socket);
    //GAgent_CheckSocketId(pgc);  //Nik.chen
    
    ret = Mqtt_Login2Server( socket,(const uint8*)username,(const uint8*)password );
    return ret;
}
/****************************************************************
        Function    :   Cloud_ResConnect
        Description :   handle packet form mqtt req connect 
        buf         :   data form mqtt.
        return      :   0: req connect ok
                        other req connect fail.
        Add by Alex.lin     --2015-03-09
****************************************************************/
uint32 Cloud_ResConnect( uint8* buf )
{
    if(NULL == buf)
        return (uint32)RET_FAILED;

    if( buf[3] == 0x00 )
    {
        if( (buf[0]!=0) && (buf[1] !=0) )
        {
        return RET_SUCCESS;
        }
        else
        {
            GAgent_Printf( GAGENT_ERROR,"%s %s %d",__FILE__,__FUNCTION__,__LINE__ );
            GAgent_Printf( GAGENT_ERROR,"MQTT Connect res  fail ret =%d!!",buf[3] );
            return (uint32)RET_FAILED;
        }
    }
    GAgent_Printf( GAGENT_ERROR,"res connect fail with %d ",buf[3] );
        return buf[3];
}
uint32 Cloud_ReqSubTopic( pgcontext pgc,uint16 mqttstatus )
{
  int32 ret=0;
  ret = Mqtt_DoSubTopic( pgc,mqttstatus);
  return ret;
}
/****************************************************************
        Function        :   Cloud_ResSubTopic
        Description     :   check sub topic respond.
        buf             :   data form mqtt.
        msgsubId        :   sub topic messages id
        return          :   0 sub topic ok.
                            other fail.
        Add by Alex.lin     --2015-03-09
****************************************************************/
uint32 Cloud_ResSubTopic( const uint8* buf,int8 msgsubId )
{
    uint16 recmsgId=0;
     if(NULL == buf)
        return (uint32)RET_FAILED;
    recmsgId = mqtt_parse_msg_id( buf );
    if( recmsgId!=msgsubId )
        return (uint32)RET_FAILED;
    else 
        return (uint32)RET_SUCCESS;
}

uint32 Cloud_Disconnect()
{
    return RET_SUCCESS;
}

uint32 Cloud_ReqDisable( pgcontext pgc )
{
    int32 ret = 0;
    int32 socket = 0;
    pgcontext pGlobalVar=NULL;
    pgconfig pConfigData=NULL;
    
    pGlobalVar=pgc;
    pConfigData = &(pgc->gc);

    pGlobalVar->rtinfo.waninfo.http_socketid = Cloud_InitSocket( pGlobalVar->rtinfo.waninfo.http_socketid,pConfigData->GServer_ip,80,0 );
    socket = pGlobalVar->rtinfo.waninfo.http_socketid;

    if( socket<=0 )
        return (uint32)RET_FAILED;


    ret = Http_Delete( socket,HTTP_SERVER,pConfigData->old_did,pConfigData->old_wifipasscode );
    return 0;
}
uint32 Cloud_ResDisable( int32 respondCode )
{
    if( 200 != respondCode )
        return 1;
    return 0;
}


uint32 Cloud_JD_Post_ReqFeed_Key( pgcontext pgc )
{
    int32 ret = 0;
    int32 socket = 0;
    pgcontext pGlobalVar=NULL;
    pgconfig pConfigData=NULL;
    
    pGlobalVar=pgc;
    pConfigData = &(pgc->gc);
    
    pGlobalVar->rtinfo.waninfo.http_socketid = Cloud_InitSocket( pGlobalVar->rtinfo.waninfo.http_socketid,pConfigData->GServer_ip,80,0 );
    socket = pGlobalVar->rtinfo.waninfo.http_socketid;
    
    if( socket<=0 )
        return (uint32)RET_FAILED;
    
    ret = Http_JD_Post_Feed_Key_req( socket,pConfigData->cloud3info.jdinfo.feed_id,pConfigData->cloud3info.jdinfo.access_key,
                                     pConfigData->DID,HTTP_SERVER );
    pConfigData->cloud3info.jdinfo.ischanged=0;
    GAgent_DevSaveConfigData( pConfigData );
    return 0;
}

uint32 Cloud_JD_Post_ResFeed_Key( pgcontext pgc,int32 respondCode )
{
    int32 ret=0;
    pgconfig pConfigData=NULL;
    
    pConfigData = &(pgc->gc);
    if( 200 != respondCode )
     return 1;
    
    if( 1 == pConfigData->cloud3info.jdinfo.ischanged )
    {
        GAgent_Printf(GAGENT_WARNING,"jd info is changed need to post again.");
        pConfigData->cloud3info.jdinfo.tobeuploaded=1;
        ret=1;
    }
    else
    {
        GAgent_Printf(GAGENT_DEBUG,"jd info post ok.");
        pConfigData->cloud3info.jdinfo.tobeuploaded=0;
        ret=0;
    }
    GAgent_DevSaveConfigData( pConfigData );
    return ret;
}
uint32 cloud_querymcuota(pgcontext pgc)
{
    /* 在以下条件下，发送失败。发送失败后，不进行重试 */
    /* 1. 云端未处于运行状态 */
    /* 2. http未空闲 */
    /* 3.  */
    int32 ret = 0;
    int32 socket = 0;

    pgc->rtinfo.waninfo.http_socketid = Cloud_InitSocket( pgc->rtinfo.waninfo.http_socketid,pgc->gc.GServer_ip,80,0 );
    socket = pgc->rtinfo.waninfo.http_socketid;

    if( socket<=0 )
        return (uint32)RET_FAILED;

    /* ret = Http_JD_Post_Feed_Key_req( socket,pConfigData->cloud3info.jdinfo.feed_id,pConfigData->cloud3info.jdinfo.access_key, */
                                     /* pConfigData->DID,HTTP_SERVER ); */
    /* pConfigData->cloud3info.jdinfo.ischanged=0; */
    /* GAgent_DevSaveConfigData( pConfigData ); */
    resetPacket(pgc->rtinfo.Txbuf);
    ret = http_querymcuota(socket, pgc->rtinfo.Txbuf->phead, HTTP_SERVER, pgc->tmcu.pk, pgc->tmcu.did, pgc->tmcu.hv, pgc->tmcu.sv, 0x02);
    GAgent_SetCloudConfigStatus( pgc,CLOUD_QUERY_MCU_OTA );
    pgc->rtinfo.waninfo.send2HttpLastTime = GAgent_GetDevTime_S();
    return 0;

}

uint32 test_trans_mcuota_cmd(pgcontext pgc)
{
    trans_sendotaresult(pgc, 0x01);
    trans_sendotaresult(pgc, 0x00);
    trans_sendotadownloadresult(pgc, TRANSCTION_OTA_FILE_DOWNLOAD_SUCCESS);
    trans_sendotadownloadresult(pgc, TRANSCTION_OTA_FILE_DOWNLOAD_FAILED);
    return 0;
}

uint32 test_cloud_querymcuota(pgcontext pgc)
{
    memset(&pgc->tmcu, 0x00, sizeof(trans_mcuotainfo));
    memcpy(pgc->tmcu.pk, "b4953ca374724b66b3686fbfb9d7c57a", strlen("6f3074fe43894547a4f1314bd7e3ae0b"));
    memcpy(pgc->tmcu.hv, "00000001", 8);
    memcpy(pgc->tmcu.sv, "01000001", 8);
    pgc->tmcu.check = 0x01;
    /* test_trans_mcuota_cmd(pgc); */
    cloud_querymcuota(pgc);
}
void Cloud_SetClientAttrs(pgcontext pgc, uint8 *clientid, uint16 cmd, int32 sn)
{
    if(NULL != clientid)
    {
        strcpy(pgc->rtinfo.waninfo.srcAttrs.phoneClientId, (int8 *)clientid);
    }
    pgc->rtinfo.waninfo.srcAttrs.cmd = cmd;
    pgc->rtinfo.waninfo.srcAttrs.sn = sn;
}
void Cloud_ClearClientAttrs(pgcontext pgc, stCloudAttrs_t *client)
{
    memset((char *)client, 0, sizeof(stCloudAttrs_t));    
}
/****************************************************************
*       functionname    :   Cloud_ReadGServerConfigData
*       description     :   read data form gserver.
*       socket          :   gserver socket.
*       buf             :   data pointer form gserver
*       buflen          :   want to read data length
        return          :   >0 data form gserver
                            other error.
        Add by Alex.lin     --2015-03-03
****************************************************************/
int32 Cloud_ReadGServerConfigData( pgcontext pgc ,int32 socket,uint8 *buf,int32 buflen )
{
    int32 ret =0;
    ret = Http_ReadSocket( socket,buf,buflen );
    if( ret <0 ) 
    {
        GAgent_Printf( GAGENT_WARNING,"Cloud_ReadGServerConfigData fail close the socket:%d",socket );
        close( socket );
        socket = INVALID_SOCKET;
        GAgent_SetGServerSocket( pgc,socket );
        return RET_FAILED;
    }
    return ret;
}


/****************************************************************
        FunctionName        :   GAgent_CloudTick.
        Description         :   GAgent Send cloud heartbeat to cloud
                                when mqttstatus is MQTT_STATUS_RUNNING

        Add by Alex.lin     --2015-03-10
****************************************************************/
void GAgent_CloudTick( pgcontext pgc,uint32 dTime_s )
{
    static uint32 count = 0;
    int ret = 0;
    uint16 newStatus=0;
    newStatus = pgc->rtinfo.GAgentStatus;
    if( 0 != pgc->rtinfo.clock )
    {
        pgc->rtinfo.clock++;
        count++;
    }
    if( count >= ONE_HOUR)
    {        
        if(CLOUD_CONFIG_OK != pgc->rtinfo.waninfo.CloudStatus)
    {
            count = ONE_HOUR-1;
        }
        else
        {
            count = 0;
            GAgent_SetCloudConfigStatus( pgc,CLOUD_REQ_GET_GSERVER_TIME );
            ret = GAgent_ReqServerTime(pgc);
            if( RET_FAILED == ret)
            {
                GAgent_Printf(GAGENT_WARNING,"Request sync time fial!\n");
            }
        }
    }
    if( WIFI_CLOUD_CONNECTED!=(newStatus&WIFI_CLOUD_CONNECTED) )
    {
        //GAgent_Printf(GAGENT_DEBUG, "GAgent_CloudTick not in WIFI_CLOUD_CONNECTED\r\n");
         return ;
    }
    
    pgc->rtinfo.waninfo.send2MqttLastTime +=dTime_s;

    GAgent_Printf(GAGENT_DEBUG, "send2MqttLastTime=%d, dTime_s=%d\r\n", (pgc->rtinfo.waninfo.send2MqttLastTime), (dTime_s));
    
    if( pgc->rtinfo.waninfo.send2MqttLastTime >= CLOUD_HEARTBEAT &&
        pgc->rtinfo.file.using == 0)
    {
    	GAgent_Printf(GAGENT_DEBUG, "send2MqttLastTime >= CLOUD_HEARTBEAT\r\n");
		
        pgc->rtinfo.waninfo.send2MqttLastTime  = 0;
        if(pgc->rtinfo.waninfo.cloudPingTime > 2 )
        {
            //ERRORCODE
            pgc->rtinfo.waninfo.cloudPingTime=0;
            pgc->rtinfo.waninfo.wanclient_num = 0;
            pgc->rtinfo.waninfo.ReConnectMqttTime = 0;
            GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_START );
            GAgent_Printf( GAGENT_INFO,"file:%s function:%s line:%d ",__FILE__,__FUNCTION__,__LINE__ );
            newStatus = GAgent_DevCheckWifiStatus( WIFI_CLOUD_CONNECTED,0 );
        }
        else
        {
            MQTT_HeartbeatTime();
            pgc->rtinfo.waninfo.cloudPingTime++;
            GAgent_Printf( GAGENT_CRITICAL,"GAgent Cloud Ping ..." );
        }
    }
    #if 0
	else
		GAgent_Printf(GAGENT_DEBUG, "send2MqttLastTime:%d CLOUD_HEARTBEAT:%d\r\n", 
				pgc->rtinfo.waninfo.send2MqttLastTime, 	CLOUD_HEARTBEAT);
    #endif
	
}
/****************************************************************
*
*   function    :   gagent do cloud config.
*   cloudstatus :   gagent cloud status.
*   return      :   0 successful other fail.
*   Add by Alex.lin --2015-02-28
****************************************************************/
uint32 Cloud_ConfigDataHandle( pgcontext pgc /*int32 cloudstatus*/ )
{
    int32 dTime=0;
    int32 ret =0;
    int32 respondCode=0;
    int32 cloudstatus = 0;
    pgcontext pGlobalVar=NULL;
    pgconfig pConfigData=NULL;

    uint16 GAgentStatus = 0;
    int8 *pDeviceID=NULL;
    int8 timeoutflag = 0;
     
    fd_set readfd;
    int32 http_fd;

    uint8 *pCloudConfiRxbuf = NULL;
    resetPacket(pgc->rtinfo.Rxbuf);
    pCloudConfiRxbuf = pgc->rtinfo.Rxbuf->phead;
    pConfigData = &(pgc->gc);
    pGlobalVar = pgc;
    cloudstatus = pgc->rtinfo.waninfo.CloudStatus;
    GAgentStatus = pgc->rtinfo.GAgentStatus;

    if((GAgentStatus&WIFI_STATION_CONNECTED) !=  WIFI_STATION_CONNECTED)
    {
       GAgent_Printf(GAGENT_CRITICAL,"####not in WIFI_STATION_CONNECTED, Cloud_ConfigData return!!!!");
        return 1 ;
    }

     #if 1
     GAgent_Printf( GAGENT_INFO,"Cloud_ConfigDataHandle:ssid=%s, key=%s \n ",pgc->gc.wifi_ssid,pgc->gc.wifi_key );
     GAgent_Printf(GAGENT_DEBUG,"GAgent M2M IP :%s \n",pgc->gc.m2m_ip );
    GAgent_Printf(GAGENT_DEBUG,"GAgent GService IP :%s \n",pgc->gc.GServer_ip );
    #endif

	
    if(strlen(pgc->gc.GServer_ip) > IP_LEN_MAX || strlen(pgc->gc.GServer_ip) < IP_LEN_MIN)
    {
        GAgent_Printf(GAGENT_CRITICAL,"GServer IP is illegal, ip=%s \n!!", (pgc->gc.GServer_ip));
        return 1;
    }

    GAgent_Printf(GAGENT_CRITICAL,"cloudstatus=%d\n", cloudstatus);
    if(CLOUD_CONFIG_OK == cloudstatus)
    {
        if(pGlobalVar->rtinfo.waninfo.http_socketid >= 0)
        {
            GAgent_Printf( GAGENT_CRITICAL,"http config ok ,and close the socket.");
            close( pGlobalVar->rtinfo.waninfo.http_socketid );
            pGlobalVar->rtinfo.waninfo.http_socketid = INVALID_SOCKET;
        }
        pgc->rtinfo.waninfo.httpCloudPingTime = 0;
        pgc->rtinfo.waninfo.ReConnectHttpTime = GAGENT_HTTP_TIMEOUT;
        return 1;
    }
    
    pDeviceID = pConfigData->DID;
    http_fd = pGlobalVar->rtinfo.waninfo.http_socketid;
    readfd  = pGlobalVar->rtinfo.readfd;

    if(CLOUD_INIT == cloudstatus)
    {
        if(strlen(pDeviceID) == (DID_LEN - 2))/*had did*/ //Nik.chen
        {
            GAgent_Printf(GAGENT_INFO,"Had did !!!!\r\n go to Provision" );
            
            ret = Cloud_ReqProvision( pgc );
            GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_PROVISION ); 
        }
        else
        {
            GAgent_Printf( GAGENT_INFO,"Need to get did!!!" );
            GAgent_SetDeviceID( pgc,NULL );/*clean did*/
            ret = Cloud_ReqRegister( pgc );
            GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_GET_DID );
        }
        pGlobalVar->rtinfo.waninfo.send2HttpLastTime = GAgent_GetDevTime_S();
        
        return 0;
    }
    dTime = abs(GAgent_GetDevTime_S()- pGlobalVar->rtinfo.waninfo.send2HttpLastTime);
    
    if( (http_fd>INVALID_SOCKET ? FD_ISSET( http_fd,&readfd ):0) &&(getselFlag())||
       ( (cloudstatus != CLOUD_CONFIG_OK) && (dTime > pgc->rtinfo.waninfo.ReConnectHttpTime))
      )
    {
        GAgent_Printf(GAGENT_DEBUG,"HTTP Data from Gserver!%d, errno=%d", 2, errno);
       // if(FD_ISSET( http_fd,&readfd ))
        //     FD_CLR(http_fd ,&readfd );
        
        if(dTime > pgc->rtinfo.waninfo.ReConnectHttpTime)
        {
            GAgent_Printf( GAGENT_INFO,"HTTP timeout, dTime=%d,reconnhttptime=%d...", dTime, (pgc->rtinfo.waninfo.ReConnectHttpTime));
            if(pGlobalVar->rtinfo.waninfo.http_socketid > 0)
            {
                close(pGlobalVar->rtinfo.waninfo.http_socketid);
                pGlobalVar->rtinfo.waninfo.http_socketid = INVALID_SOCKET;
            }
            respondCode = -1;
        }
        else
        {
            ret = Cloud_ReadGServerConfigData( pgc,pGlobalVar->rtinfo.waninfo.http_socketid,pCloudConfiRxbuf,1024 );
            if(ret <= 0)
            {
                if(pGlobalVar->rtinfo.waninfo.http_socketid > 0)
                {
                    close(pGlobalVar->rtinfo.waninfo.http_socketid);
                    pGlobalVar->rtinfo.waninfo.http_socketid = INVALID_SOCKET;
                    GAgent_SetGServerSocket( pgc,pGlobalVar->rtinfo.waninfo.http_socketid );
                }
                respondCode = -1;
            }
            else
            {
                respondCode = Http_Response_Code( pCloudConfiRxbuf );
            }
        }

        GAgent_Printf(GAGENT_INFO,"http read ret:%d cloudStatus : %d Response code: %d",ret,cloudstatus,respondCode );
        switch( cloudstatus )
        {
            case CLOUD_RES_GET_DID:
                 ret = Cloud_ResRegister( pCloudConfiRxbuf,ret,pDeviceID,respondCode ); 
                 if(RET_SUCCESS != ret)/* can't got the did */
                 {
                     if(dTime > pgc->rtinfo.waninfo.ReConnectHttpTime)
                     {
                         timeoutflag = 1;
                         GAgent_Printf(GAGENT_ERROR,"res register fail: %s %d",__FUNCTION__,__LINE__ );
                         GAgent_Printf(GAGENT_ERROR,"go to req register Device id again.");
                         ret = Cloud_ReqRegister( pgc );
                     }
                 }
                 else
                 {
                     pgc->rtinfo.waninfo.ReConnectHttpTime = GAGENT_HTTP_TIMEOUT;
                     pgc->rtinfo.waninfo.httpCloudPingTime = 0;
                     pgc->rtinfo.waninfo.firstConnectHttpTime = GAgent_GetDevTime_S();
                     GAgent_SetDeviceID( pgc,pDeviceID );
                     GAgent_DevGetConfigData( &(pgc->gc) );
                     GAgent_Printf( GAGENT_DEBUG,"Register got did :%s len=%d",pgc->gc.DID,strlen(pgc->gc.DID) );
                     GAgent_Printf( GAGENT_DEBUG,"GAgent go to Provision!!!");
                     
                     ret = Cloud_ReqProvision( pgc );
                     GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_PROVISION ); 
                 }
                 break;
            case CLOUD_RES_PROVISION:
                 pGlobalVar->rtinfo.waninfo.Cloud3Flag = Http_Get3rdCloudInfo( pConfigData->cloud3info.cloud3Name,pConfigData->cloud3info.jdinfo.product_uuid ,
                                                                pCloudConfiRxbuf );
                 /* have 3rd cloud info need save to falsh */
                 if( pGlobalVar->rtinfo.waninfo.Cloud3Flag == 1 )
                 {
                    GAgent_Printf(GAGENT_INFO,"3rd cloud name:%s",pConfigData->cloud3info.cloud3Name );
                    GAgent_Printf(GAGENT_INFO,"3re cloud UUID: %s",pConfigData->cloud3info.jdinfo.product_uuid);
                    GAgent_DevSaveConfigData( pConfigData );
                 }
                 ret = Cloud_ResProvision( pGlobalVar->minfo.m2m_SERVER , &pGlobalVar->minfo.m2m_Port,pCloudConfiRxbuf,respondCode);
                 GAgent_Get_Gserver_Time( &pGlobalVar->rtinfo.clock, pCloudConfiRxbuf,respondCode );
                 if( ret!=0 )
                 {
                    if(dTime > pgc->rtinfo.waninfo.ReConnectHttpTime)
                    {
				timeoutflag = 1;
				int8 taskData[DID_LEN+PASSCODE_LEN+1];
			      memset(taskData, 0, sizeof(taskData));  
			      memcpy(&taskData[0], RECONNHTTPGLAG1, strlen(RECONNHTTPGLAG1)); 
		            memcpy(&taskData[DID_LEN], RECONNHTTPGLAG2, strlen(RECONNHTTPGLAG2)); 
				
				GAgent_Printf(GAGENT_WARNING,"Provision res fail ret=%d.", ret );
				GAgent_Printf(GAGENT_WARNING,"go to provision again.");
	
				#if 1
				if(4 != getDnsFlag())
				{
				setDnsFlag(4);
				CC3200_DNSTaskDelete();
				ret = CC3200_CreateDnsTask(taskData);
				if(ret < 0)
				{
				setDnsFlag(0);
				}

				}
				else
					msleep(200);
				#else
				ret = Cloud_ReqProvision( pgc );
				#endif
                    }
                 }
                 else
                 {
                    pgc->rtinfo.waninfo.ReConnectHttpTime = GAGENT_HTTP_TIMEOUT;
                    pgc->rtinfo.waninfo.httpCloudPingTime = 0;
                    pgc->rtinfo.waninfo.firstConnectHttpTime = GAgent_GetDevTime_S();

			 if(4 == getDnsFlag())
		          {
			        GAgent_Printf( GAGENT_INFO," Provision OK, kill DnsParsingTick task!!! ");
				  pgc->rtinfo.waninfo.RefreshIPTime =  ONE_HOUR;//Nik.chen ONE_HOUR;
				  setDnsFlag(1);
			        CC3200_DNSTaskDelete();
			    }
                
                    //login to m2m.
                    GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_START );
                pgc->rtinfo.waninfo.ReConnectMqttTime = 0;
                pgc->rtinfo.waninfo.send2MqttLastTime = GAgent_GetDevTime_S();
                ret = Cloud_ReqGetSoftver( pgc,pgc->rtinfo.OTATypeflag);
                    GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_GET_SOFTVER );  

                    GAgent_Printf(GAGENT_INFO,"Provision OK!");
                    GAgent_Printf(GAGENT_INFO,"M2M host:%s port:%d",pGlobalVar->minfo.m2m_SERVER,pGlobalVar->minfo.m2m_Port);
                    GAgent_Printf(GAGENT_INFO,"GAgent go to update OTA info and login M2M !");
                  }
                 break;
            case CLOUD_RES_GET_SOFTVER:
                {
                    /*
                      鑾峰彇OTA淇℃伅閿欒杩涘叆provision 鎴愬姛鍒欒繘琛孫TA.
                    */
                    int8 *download_url = NULL;
                    int8  disableDIDflag=0;
                int32 i;
                    download_url = (int8 *)malloc(256);
                    if(NULL == download_url)
                    {
                    GAgent_Printf(GAGENT_WARNING, "OTA malloc fail!go to get OTA info again...");
                        break;
                    }
                ret = Cloud_ResGetSoftver( download_url ,pGlobalVar->gc.FirmwareVer ,pCloudConfiRxbuf,respondCode );
                    if( RET_SUCCESS != ret )
                    {
                        if(dTime > pgc->rtinfo.waninfo.ReConnectHttpTime)
                        {
                            timeoutflag = 1;
                    ret = Cloud_ReqGetSoftver( pgc, pgc->rtinfo.OTATypeflag );
                        GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_GET_SOFTVER );
                            GAgent_Printf( GAGENT_WARNING,"GAgent get OTA info Timeout do it again! ");
                        }
                        else
                    {
                    if( OTATYPE_MCU == pgc->rtinfo.OTATypeflag )
                        {
                        GAgent_Printf( GAGENT_WARNING,"GAgent get MCU OTA respondCode:%d",respondCode );
                        if(0 == pgc->rtinfo.onlinePushflag)
                        {
                            GAgent_Printf( GAGENT_WARNING,"go to check disaable Device!" );
                            disableDIDflag=1;
                        }
                        else
                        {
                            GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK);
                        }  
                    }
                    else
                    {
                        GAgent_Printf( GAGENT_WARNING,"GAgent get WIFI OTA respondCode:%d",respondCode );
                        if(0 == pgc->rtinfo.onlinePushflag)
                        {
                            GAgent_Printf( GAGENT_WARNING,"go to check MCU OTA!" );
                            pgc->rtinfo.OTATypeflag = OTATYPE_MCU;
                            Cloud_ReqGetSoftver( pgc, pgc->rtinfo.OTATypeflag );
                            GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_GET_SOFTVER );
                        }
                        else
                        {
                            GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK);
                        }
                        }
                    }
                }
                    else
                    {
                        pgc->rtinfo.waninfo.ReConnectHttpTime = GAGENT_HTTP_TIMEOUT;
                        pgc->rtinfo.waninfo.httpCloudPingTime = 0;
                        pgc->rtinfo.waninfo.firstConnectHttpTime = GAgent_GetDevTime_S();
                	ret = Cloud_isNeedOTA( pgc, pgc->rtinfo.OTATypeflag, pGlobalVar->gc.FirmwareVer );
                    if( ret != RET_SUCCESS )
                    {
                    if( OTATYPE_MCU == pgc->rtinfo.OTATypeflag )
                    {
                        GAgent_Printf(GAGENT_WARNING,"MCU does not need OTA,current MCU softver = %s",pgc->mcu.soft_ver);
                        if(0 == pgc->rtinfo.onlinePushflag)
                        {
                            GAgent_Printf( GAGENT_WARNING,"go to check disaable Device!" );
                            disableDIDflag=1;
                        }
                        else
                        {
                            GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK);
                        }  
                    }
                    else
                    {
                        GAgent_Printf( GAGENT_WARNING,"WIFI does not need OTA,current WIFI softver = %s",WIFI_SOFTVAR );
                        if(0 == pgc->rtinfo.onlinePushflag)
                        {
                            GAgent_Printf( GAGENT_WARNING,"go to check MCU OTA!" );
                            pgc->rtinfo.OTATypeflag = OTATYPE_MCU;
                            Cloud_ReqGetSoftver( pgc, pgc->rtinfo.OTATypeflag );
                            GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_GET_SOFTVER );
                        }
                        else
                        {
                            GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK);
                        }
                    }
                }
                    else
                    {
                        GAgent_Printf( GAGENT_CRITICAL,"Start OTA, type=%d...\n", (pgc->rtinfo.OTATypeflag));
                    if( RET_SUCCESS == GAgent_Cloud_OTAByUrl( pgc, download_url, pgc->rtinfo.OTATypeflag ) )
                        {
                        if( OTATYPE_MCU == pgc->rtinfo.OTATypeflag )
                            {
                                GAgent_Printf( GAGENT_CRITICAL,"GAgent Download MCU Firmware success!\n");
                                resetPacket(pgc->rtinfo.Rxbuf);
                                *(uint32 *)(pgc->rtinfo.Rxbuf->ppayload) = htonl(pgc->rtinfo.filelen);
                                *(uint16 *)(pgc->rtinfo.Rxbuf->ppayload+4) = htons(32);
                                for(i=0; i<32; i++)
                                    pgc->rtinfo.Rxbuf->ppayload[4+2+i] = pgc->mcu.MD5[i];
                                pgc->rtinfo.Rxbuf->pend = (pgc->rtinfo.Rxbuf->ppayload) + 4 + 2 + 32;
                            copyPacket(pgc->rtinfo.Rxbuf, pgc->rtinfo.Txbuf);
                            GAgent_LocalDataWriteP0( pgc,pgc->rtinfo.local.uart_fd, pgc->rtinfo.Txbuf, MCU_NEED_UPGRADE );
                            if(0 == pgc->rtinfo.onlinePushflag)
                            {
                                GAgent_Printf( GAGENT_WARNING,"go to check disaable Device!" );
                                disableDIDflag=1;
                            }
                            else
                            {
                                GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK);
                            } 
                        }
                        else
                        {
                            GAgent_Printf( GAGENT_CRITICAL,"GAgent download WIFI firmware success!" );
                            if(0 == pgc->rtinfo.onlinePushflag)
                            {
                                GAgent_Printf( GAGENT_WARNING,"go to check MCU OTA!" );
                                pgc->rtinfo.OTATypeflag = OTATYPE_MCU;
                                Cloud_ReqGetSoftver( pgc, pgc->rtinfo.OTATypeflag );
                                GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_GET_SOFTVER );
                            }
                            else
                            {
                                GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK);
                            }
                        }
                    }
                        else
                        {
                        if( OTATYPE_MCU == pgc->rtinfo.OTATypeflag )
                            {
                            GAgent_Printf(GAGENT_WARNING,"GAgent download MCU firmware failed!");
				  if(0 == getDnsFlag()||1 == getDnsFlag())
	                        	{
	                            CC3200_CreateDnsTask(NULL);
	                        	}
                            if(0 == pgc->rtinfo.onlinePushflag)
                            {
                                GAgent_Printf(GAGENT_WARNING,"go to check disaable Device!");
                            disableDIDflag=1;
                            }
                            else
                            {
                                GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK);
                            }
                        }
                        else
                        {
                            GAgent_Printf(GAGENT_WARNING,"GAgent download WIFI firmware failed!");
                            if(0 == getDnsFlag()||1 == getDnsFlag())
	                        	{
	                            CC3200_CreateDnsTask(NULL);
	                        	}
                            if(0 == pgc->rtinfo.onlinePushflag)
                            {
                                GAgent_Printf(GAGENT_WARNING,"go to check MCU OTA!");
                                pgc->rtinfo.OTATypeflag = OTATYPE_MCU;
                                Cloud_ReqGetSoftver( pgc, pgc->rtinfo.OTATypeflag );
                                GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_GET_SOFTVER );
                            }
                            else
                            {
                                GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK);
                            }
                            } 
                        }
                    }
                }
                if( (1==disableDIDflag) )
                    {
                        if( 1==GAgent_IsNeedDisableDID( pgc ) )
                        {
                            GAgent_Printf(GAGENT_INFO,"Need to Disable Device ID!");
                            ret = Cloud_ReqDisable( pgc );
                            GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_DISABLE_DID );
                        }
                    else
                    {
                    GAgent_SetCloudConfigStatus ( pgc,CLOUD_CONFIG_OK );
                    }
                pgc->rtinfo.OTATypeflag = OTATYPE_WIFI;
                }
                    free(download_url);
                    break;
                }
            case CLOUD_RES_DISABLE_DID:
                 ret = Cloud_ResDisable( respondCode );
                 if(ret!=0)
                 {
                     if(dTime > pgc->rtinfo.waninfo.ReConnectHttpTime)
                     {
                        timeoutflag = 1;
                        GAgent_Printf(GAGENT_WARNING,"Disable Device ID Fail.");
                       if( 1==GAgent_IsNeedDisableDID( pgc ) )
                        {
                                    GAgent_Printf(GAGENT_INFO,"Need to Disable Device ID again !");
                                    ret = Cloud_ReqDisable( pgc );
                                    GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_DISABLE_DID );
                        }
                        else
                        {
                           if( 1==pConfigData->cloud3info.jdinfo.tobeuploaded )
                            {
                                GAgent_Printf( GAGENT_INFO,"%d Neet to be uploaded jdinfo.",__LINE__);
                                GAgent_SetCloudConfigStatus ( pgc,CLOUD_RES_POST_JD_INFO );
                            }
                        }
                     }
                     else
                     {
                       if( 1==pConfigData->cloud3info.jdinfo.tobeuploaded )
                       {
                            GAgent_Printf( GAGENT_INFO,"%d Neet to be uploaded jdinfo.",__LINE__);
                            GAgent_SetCloudConfigStatus ( pgc,CLOUD_RES_POST_JD_INFO );
                       }
                       else
                       {
                        GAgent_SetCloudConfigStatus ( pgc,CLOUD_CONFIG_OK );
                       }
                     }
                 }
                 else
                 {
                    pgc->rtinfo.waninfo.ReConnectHttpTime = GAGENT_HTTP_TIMEOUT;
                    pgc->rtinfo.waninfo.httpCloudPingTime = 0;
                    pgc->rtinfo.waninfo.firstConnectHttpTime = GAgent_GetDevTime_S();
                    GAgent_Printf(GAGENT_INFO,"Disable Device ID OK!");
                    GAgent_SetOldDeviceID( pgc,NULL,NULL,0 );
                    if( 1==pConfigData->cloud3info.jdinfo.tobeuploaded )
                    {
                        GAgent_Printf( GAGENT_INFO,"%d Neet to be uploaded jdinfo.",__LINE__);
                        GAgent_SetCloudConfigStatus ( pgc,CLOUD_RES_POST_JD_INFO );
                    }
                    else
                    {
                    GAgent_SetCloudConfigStatus ( pgc,CLOUD_CONFIG_OK );
                    }
                 }
                  
                 break;
            case CLOUD_RES_POST_JD_INFO:
                 ret = Cloud_JD_Post_ResFeed_Key( pgc,respondCode );
                 if( ret!=0 )
                 {
                     GAgent_Printf( GAGENT_WARNING," Post JD info respond fail!" );
                     //if( dTime > pgc->rtinfo.waninfo.ReConnectHttpTime )
                     {
                        timeoutflag = 1;
                        GAgent_Printf( GAGENT_WARNING," Post JD info again");
                        ret = Cloud_JD_Post_ReqFeed_Key( pgc );
                     }
                 }
                 else
                 {
                    pgc->rtinfo.waninfo.ReConnectHttpTime = GAGENT_HTTP_TIMEOUT;
                    pgc->rtinfo.waninfo.httpCloudPingTime = 0;
                    pgc->rtinfo.waninfo.firstConnectHttpTime = GAgent_GetDevTime_S();
                    GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK );
                 }
                 break;
            case CLOUD_REQ_GET_GSERVER_TIME:
                GAgent_Get_Gserver_Time( &pGlobalVar->rtinfo.clock, pCloudConfiRxbuf,respondCode );
                GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK );
		    break;
        case CLOUD_QUERY_MCU_OTA:
            GAgent_Printf(GAGENT_DEBUG, "transction==MCU OTA check response code  %d", respondCode);
            if(respondCode != 200)
            {
                GAgent_Printf(GAGENT_DEBUG, "transction==MCU OTA check failed");
                trans_sendotadownloadresult(pgc, TRANSCTION_OTA_FILE_DOWNLOAD_FAILED);
                memset(&pgc->tmcu, 0x00, sizeof(pgc->tmcu));
            }
            else
            {
                GAgent_Printf(GAGENT_DEBUG, "transction==MCU OTA check success");
                u8 sv[10] = {0};
                u8 url[256] = {0};
                Http_GetSoftver_Url(url, sv, pCloudConfiRxbuf);
                trans_checkmcuota(pgc, sv, url);
            }
            GAgent_SetCloudConfigStatus( pgc,CLOUD_CONFIG_OK );
            break;
            default:
                break;
        }
        
        if(timeoutflag)
        { 
            pgc->rtinfo.waninfo.ReConnectHttpTime += (10 * ONE_SECOND);
            pgc->rtinfo.waninfo.httpCloudPingTime++;
            if(pgc->rtinfo.waninfo.httpCloudPingTime == 10)
            {
            
		timeoutflag = 1;
		int8 taskData[DID_LEN+PASSCODE_LEN+1];
		memset(taskData, 0, sizeof(taskData));  
		memcpy(&taskData[0], RECONNHTTPGLAG1, strlen(RECONNHTTPGLAG1)); 
		memcpy(&taskData[DID_LEN], RECONNHTTPGLAG2, strlen(RECONNHTTPGLAG2)); 

		GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_PROVISION ); 
		pgc->rtinfo.waninfo.httpCloudPingTime = 0;

		 #if 1
		 if(4 != getDnsFlag())
		 {
		 setDnsFlag(4);
		 CC3200_DNSTaskDelete();
		 ret = CC3200_CreateDnsTask(taskData);
		 if(ret < 0)
		 {
		    setDnsFlag(0);
		 }

		 }
		  else
		 	msleep(200);
		#else
		 ret = Cloud_ReqProvision( pgc );
	      #endif

            }

		GAgent_Printf(GAGENT_DEBUG, "firstConnectHttpTime=%d !!!", (pgc->rtinfo.waninfo.firstConnectHttpTime), GAgent_GetDevTime_S());

            if((GAgent_GetDevTime_S()-pgc->rtinfo.waninfo.firstConnectHttpTime) >= 2*ONE_HOUR) //Nik.chen 2 * ONE_HOUR
            {
                setconnFlag(0); 
                GAgent_Printf(GAGENT_DEBUG, "disconnect from http server more than 2hours!!!");
                GAgent_DevReset();
            }
        }  
        pGlobalVar->rtinfo.waninfo.send2HttpLastTime = GAgent_GetDevTime_S(); 
    }
  
    return 0;
}

/****************************************************************
        FunctionName        :   Cloud_M2MDataHandle.
        Description         :   Receive cloud business data .
        xpg                 :   global context.
        Rxbuf                :   global buf struct.
        buflen              :   receive max len.
        return              :   >0 have business data,and need to 
                                   handle.
                                other,no business data.
        Add by Alex.lin     --2015-03-10
****************************************************************/
int32 Cloud_M2MDataHandle(  pgcontext pgc,ppacket pbuf /*, ppacket poutBuf*/, uint32 buflen)
{
    uint32 dTime=0,ret=0,dataLen=0;
    int32 packetLen=0;
    pgcontext pGlobalVar=NULL;
    pgconfig pConfigData=NULL;
    int8 *username=NULL;
    int8 *password=NULL;
    uint8* pMqttBuf=NULL;
    fd_set readfd;
    int32 mqtt_fd=-1;
    static int8 connFlag = 0;
 
    uint16 mqttstatus=0;
    uint8 mqttpackType=0;

    
    pConfigData = &(pgc->gc);
    pGlobalVar = pgc;
        
    mqttstatus = pGlobalVar->rtinfo.waninfo.mqttstatus;
    mqtt_fd = pGlobalVar->rtinfo.waninfo.m2m_socketid;
    readfd  = pGlobalVar->rtinfo.readfd;
    username = pConfigData->DID;
    password = pConfigData->wifipasscode;
    
    if( strlen(pConfigData->m2m_ip)==0 )
    {
        GAgent_Printf( GAGENT_INFO,"M2M IP =0 IP TIME 1 %d 2%d ",pgc->rtinfo.waninfo.RefreshIPLastTime,pgc->rtinfo.waninfo.RefreshIPTime);
        return 0;
    }

    //FD_ZERO(&readfd);
    //FD_SET(mqtt_fd, &readfd);

		
    dTime = abs( GAgent_GetDevTime_S()-pgc->rtinfo.waninfo.send2MqttLastTime );
    if( MQTT_STATUS_START==mqttstatus&& !((pgc->rtinfo.GAgentStatus)&WIFI_MODE_AP))
    {
        if( dTime > pgc->rtinfo.waninfo.ReConnectMqttTime )
        {
        	int8 taskData[DID_LEN+PASSCODE_LEN+1];
	      memset(taskData, 0, sizeof(taskData));  
	      memcpy(&taskData[0], username, DID_LEN); 
            memcpy(&taskData[DID_LEN], password, strlen(password)); 		  
            GAgent_Printf(GAGENT_INFO,"Req to connect m2m, jump to another task to reconnect !");
            GAgent_Printf(GAGENT_INFO,"username: %s password: %s",username,password);

		#if 1
		 if(4 != getDnsFlag())
		 {
		 setDnsFlag(4);
		 CC3200_DNSTaskDelete();
		 ret = CC3200_CreateDnsTask(taskData);
		 if(ret < 0)
		 {
		    setDnsFlag(0);
		 }
		
		 }
		  else
		 	msleep(200);
		#else
		Cloud_ReqConnect( pgc,username,password );
		#endif
            
            GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_RES_LOGIN );
            GAgent_Printf(GAGENT_INFO," ReConnectMqttTime=%d ", pgc->rtinfo.waninfo.ReConnectMqttTime);
            pgc->rtinfo.waninfo.send2MqttLastTime = GAgent_GetDevTime_S();
            if( pgc->rtinfo.waninfo.ReConnectMqttTime < GAGENT_MQTT_TIMEOUT)
            {
                pgc->rtinfo.waninfo.ReConnectMqttTime = GAGENT_MQTT_TIMEOUT;
            }
            else
            {
            pgc->rtinfo.waninfo.ReConnectMqttTime+=GAGENT_CLOUDREADD_TIME;
                if(pgc->rtinfo.waninfo.ReConnectMqttTime > GAGENT_MQTT_DEADTIME_IN_TIMEOUT) //Nik.chen GAGENT_MQTT_DEADTIME_IN_TIMEOUT
                {
                    GAgent_Printf(GAGENT_CRITICAL, "111111111 disconnect to m2m server over 2 h.reset!!!\r\n");
                    sleep(1);
                    GAgent_DevReset();
                }
            }
        }
        return 0;
    }
    
    if( ((mqtt_fd>INVALID_SOCKET ? FD_ISSET( mqtt_fd,&readfd ):0) &&getselFlag())||
        ( mqttstatus!=MQTT_STATUS_RUNNING && dTime>(pgc->rtinfo.waninfo.ReConnectMqttTime) )
      )
    {
        if( mqtt_fd>INVALID_SOCKET ? FD_ISSET( mqtt_fd,&readfd):0 ) 
        {
         // FD_CLR(mqtt_fd ,&readfd  );
          resetPacket( pbuf );
          pMqttBuf = pbuf->phead;
          GAgent_Printf(GAGENT_DEBUG,"Data form M2M!!! and fd is:%d", mqtt_fd);
          packetLen = MQTT_readPacket(mqtt_fd,pbuf,GAGENT_BUF_LEN );
          if( packetLen==-1 ) 
          {
              mqtt_fd=INVALID_SOCKET;
              //mqtt_sel_fd=INVALID_SOCKET;
              pGlobalVar->rtinfo.waninfo.m2m_socketid=INVALID_SOCKET;
        
              //GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_START );
               if( ((pgc->rtinfo.GAgentStatus)&WIFI_CLOUD_CONNECTED) != WIFI_CLOUD_CONNECTED )
                {
                    GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_START );
                    pgc->rtinfo.waninfo.ReConnectMqttTime = 0;
                }
              GAgent_DevCheckWifiStatus( WIFI_CLOUD_CONNECTED,0 );
              GAgent_Printf(GAGENT_DEBUG,"MQTT fd was closed!!");
              GAgent_Printf(GAGENT_DEBUG,"GAgent go to MQTT_STATUS_START");
              return RET_FAILED;
          }
          else if( packetLen>0 )
          {
            pgc->rtinfo.waninfo.ReConnectMqttTime = GAGENT_MQTT_TIMEOUT;
            mqttpackType = MQTTParseMessageType( pMqttBuf );
            GAgent_Printf( GAGENT_DEBUG,"MQTT message type %d",mqttpackType );
          }
          else
          {
            return RET_FAILED;
          }
        }

        /*create mqtt connect to m2m.*/
        if( MQTT_STATUS_RUNNING!=mqttstatus &&
            (MQTT_MSG_CONNACK==mqttpackType||MQTT_MSG_SUBACK==mqttpackType||dTime>(pgc->rtinfo.waninfo.ReConnectMqttTime) ) )
        {
            int8 timeoutFlag=0;
            switch( mqttstatus)
            {
                case MQTT_STATUS_RES_LOGIN:
                     ret = Cloud_ResConnect( pMqttBuf );
                     if( RET_SUCCESS!= ret )
                     {
                         GAgent_Printf(GAGENT_DEBUG," MQTT_STATUS_REQ_LOGIN Fail ");
                         if( dTime > (pgc->rtinfo.waninfo.ReConnectMqttTime) )
                         {
                            timeoutFlag =1;
			         int8 taskData[DID_LEN+PASSCODE_LEN+1];
			         memset(taskData, 0, sizeof(taskData));  
			         memcpy(&taskData[0], username, DID_LEN); 
			         memcpy(&taskData[DID_LEN], password, strlen(password)); 	
                            GAgent_Printf(GAGENT_DEBUG,"MQTT req connetc m2m again!"); 
				 #if 1
				 if(4 != getDnsFlag())
				 {
				 setDnsFlag(4);
				 CC3200_DNSTaskDelete();
				 ret = CC3200_CreateDnsTask(taskData);
				 if(ret < 0)
				 {
				    setDnsFlag(0);
				 }
				 else
				  {
					GAgent_Printf(GAGENT_DEBUG,"Cloud_ResConnect failed! CC3200_CreateDnsTask");
					//msleep(200);
				  }
				
				 }
				  else
				  {
					GAgent_Printf(GAGENT_DEBUG,"Cloud_ResConnect failed! sleep 200 ms");
					msleep(200);
				  }
				#else
				Cloud_ReqConnect( pgc,username,password );
				#endif
                         }
                     }
                     else
                     {
                         GAgent_Printf(GAGENT_DEBUG,"GAgent do req connect m2m OK !");
                         GAgent_Printf(GAGENT_DEBUG,"Go to MQTT_STATUS_REQ_LOGINTOPIC1. ");
			     if(4 == getDnsFlag())
		          {
			        GAgent_Printf( GAGENT_INFO," M2M reconnect ok, kill DnsParsingTick task!!! ");
			        CC3200_DNSTaskDelete();
				   setDnsFlag(2);
			    }
                         Cloud_ReqSubTopic( pgc,MQTT_STATUS_REQ_LOGINTOPIC1 );
                         GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_RES_LOGINTOPIC1 );
                     }
                     break;
                case MQTT_STATUS_RES_LOGINTOPIC1:
                     ret = Cloud_ResSubTopic(pMqttBuf,pgc->rtinfo.waninfo.mqttMsgsubid );
                     if( RET_SUCCESS!=ret )
                     {
                        GAgent_Printf(GAGENT_DEBUG," MQTT_STATUS_RES_LOGINTOPIC1 Fail ");
                        if( dTime > (pgc->rtinfo.waninfo.ReConnectMqttTime) )
                        {
                            timeoutFlag =1;
                            GAgent_Printf( GAGENT_DEBUG,"GAgent req sub LOGINTOPIC1 again ");
                            Cloud_ReqSubTopic( pgc,MQTT_STATUS_REQ_LOGINTOPIC1 );
                        }
                     }
                     else
                     {
                        GAgent_Printf(GAGENT_DEBUG,"Go to MQTT_STATUS_RES_LOGINTOPIC2. ");
                        Cloud_ReqSubTopic( pgc,MQTT_STATUS_REQ_LOGINTOPIC2 );
                        GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_RES_LOGINTOPIC2 );
                     }
                     break;
                case MQTT_STATUS_RES_LOGINTOPIC2:
                     ret = Cloud_ResSubTopic(pMqttBuf,pgc->rtinfo.waninfo.mqttMsgsubid );
                     if( RET_SUCCESS != ret )
                     {
                        GAgent_Printf(GAGENT_DEBUG," MQTT_STATUS_RES_LOGINTOPIC2 Fail ");
                        if( dTime > (pgc->rtinfo.waninfo.ReConnectMqttTime) )
                        {
                            timeoutFlag =1;
                            GAgent_Printf( GAGENT_INFO,"GAgent req sub LOGINTOPIC2 again.");
                            Cloud_ReqSubTopic( pgc,MQTT_STATUS_REQ_LOGINTOPIC2 );
                        }
                     }
                     else
                     {
                        GAgent_Printf(GAGENT_DEBUG," Go to MQTT_STATUS_RES_LOGINTOPIC3. ");
                        Cloud_ReqSubTopic( pgc,MQTT_STATUS_REQ_LOGINTOPIC3 );
                        GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_RES_LOGINTOPIC3 );
                     }
                     break;
                case MQTT_STATUS_RES_LOGINTOPIC3:
                      ret = Cloud_ResSubTopic(pMqttBuf,pgc->rtinfo.waninfo.mqttMsgsubid );
                     if(RET_SUCCESS!= ret )
                     {
                        GAgent_Printf(GAGENT_DEBUG," MQTT_STATUS_RES_LOGINTOPIC3 Fail ");
                        if( dTime > (pgc->rtinfo.waninfo.ReConnectMqttTime) )
                        {
                            timeoutFlag =1;
                            GAgent_Printf(GAGENT_DEBUG,"GAgent req sub LOGINTOPIC3 again." );
                            Cloud_ReqSubTopic( pgc,MQTT_STATUS_REQ_LOGINTOPIC3 );
                        }
                     }
                     else
                     {
                        GAgent_Printf(GAGENT_CRITICAL,"GAgent Cloud Working...");
                        GAgent_SetCloudServerStatus( pgc,MQTT_STATUS_RUNNING );
                        GAgent_Printf( GAGENT_INFO,"file:%s function:%s line:%d ",__FILE__,__FUNCTION__,__LINE__ );
                        GAgent_DevCheckWifiStatus( WIFI_CLOUD_CONNECTED,1 );
                     }
                      break;
                default:
                     break;
            }
            if( 1==timeoutFlag )
            {
            	    GAgent_Printf(GAGENT_CRITICAL, "222222222222 ReConnectMqttTime=%d!!!\r\n", pgc->rtinfo.waninfo.ReConnectMqttTime);
                pgc->rtinfo.waninfo.ReConnectMqttTime+=GAGENT_CLOUDREADD_TIME;
                if(pgc->rtinfo.waninfo.ReConnectMqttTime > GAGENT_MQTT_DEADTIME_IN_TIMEOUT) //GAGENT_MQTT_DEADTIME_IN_TIMEOUT
                {
                    GAgent_Printf(GAGENT_CRITICAL, "222222222222 disconnect to m2m server over 2 h.reset!!!\r\n");
                    sleep(1);
                    GAgent_DevReset();
                }
            }
            pgc->rtinfo.waninfo.send2MqttLastTime = GAgent_GetDevTime_S(); 
        }
        else if( packetLen>0 && ( mqttstatus == MQTT_STATUS_RUNNING ) )
        {
            //int varlen=0,p0datalen=0;
            switch( mqttpackType )
            {
                case MQTT_MSG_PINGRESP:
                    pgc->rtinfo.waninfo.cloudPingTime=0;
                    GAgent_Printf(GAGENT_CRITICAL,"GAgent Cloud Pong ... \r\n");
                break;
                case MQTT_MSG_PUBLISH:
                    dataLen = Mqtt_DispatchPublishPacket( pgc,pMqttBuf,packetLen );
                    if( dataLen>0 )
                    {
                        pbuf->type = SetPacketType( pbuf->type,CLOUD_DATA_IN,1 );
                        ParsePacket(  pbuf );
                        GAgent_Printf(GAGENT_INFO,"%s %d type : %04X len :%d",__FUNCTION__,__LINE__,pbuf->type,dataLen );
                    }
                break;
                default:
                    GAgent_Printf(GAGENT_WARNING," data form m2m but msg type is %d",mqttpackType );
                	break;
            }
        }
    }
    return dataLen;
}

int32 GAgent_Cloud_GetPacket( pgcontext pgc,ppacket pRxbuf, int32 buflen)
{
	int32 Mret=0;
	uint16 GAgentstatus = 0;
    ppacket pbuf = pRxbuf;
	GAgentstatus = pgc->rtinfo.GAgentStatus;
   
	if( (GAgentstatus&WIFI_STATION_CONNECTED) != WIFI_STATION_CONNECTED)
	    return -1 ;
    if( WIFI_MODE_TEST==(GAgentstatus&WIFI_MODE_TEST) )
        return -1;
	//GAgent_Printf(GAGENT_WARNING, "@@@@@@@@@@@@@@m2m socket id:%d\r\n", pgc->rtinfo.waninfo.m2m_socketid);
	
	
		
	Cloud_ConfigDataHandle( pgc );

	Mret = Cloud_M2MDataHandle( pgc,pbuf, buflen );
	    return Mret;
}
void GAgent_Cloud_Handle( pgcontext pgc, ppacket Rxbuf,int32 length )
{
    int32 cloudDataLen = 0;

    cloudDataLen = GAgent_Cloud_GetPacket( pgc,Rxbuf ,length );
    if( cloudDataLen>0 )
    {
        dealPacket(pgc, Rxbuf);        
        Cloud_ClearClientAttrs(pgc, &pgc->rtinfo.waninfo.srcAttrs);
        clearChannelAttrs(pgc);
    }
}
