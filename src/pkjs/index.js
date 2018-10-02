var devMode = false;

const JS_GET_STATS = 1;
const JS_SEND_PULSE = 2;
const JS_GET_MAP = 3;
const JS_GET_NEXT_MAP = 4;
const JS_STATUS_READY = 0;
const JS_STATUS_STATS_OK = 1;
const JS_STATUS_PULSE_OK = 2;
const JS_STATUS_MAP_OK = 3;
const JS_STATUS_NEXT_MAP_OK = 4;

var requestCode;
var mapData;
var mapPartCtr=0;


// Listen for when an AppMessage is received
Pebble.addEventListener('appmessage',
  function(e) {
    requestCode = e.payload['CODE']
    console.log('AppMessage received! code '+requestCode);
    if(requestCode==JS_GET_STATS) {
      getStats();
    } else if(requestCode==JS_GET_MAP) {
      getMap();
    } else if(requestCode==JS_GET_NEXT_MAP) {
      setTimeout(function(){
      getNextMap();
      },500);
    } else if(requestCode==JS_SEND_PULSE) {
      sendPulse();
    }
  }                     
);


var xhrRequest = function (url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    callback(this.responseText);
  };
  xhr.open(type, url);
  xhr.send();
};

function locationSuccess(pos) {
  webPulse(pos);
}

function locationError(err) {
  console.log('Error requesting location!');
  webPulse(false);
}
function webPulse(pos) {
  // Construct URL
  var myAPIKey = Pebble.getWatchToken();
  var url = 'http://darkestmon.100webspace.net/pebble/pia.php?act=pulse&id=' + myAPIKey;
  if(pos)
    url += '&lat=' + Math.round(pos.coords.latitude) + '&long=' + Math.round(pos.coords.longitude);
  if(requestCode==JS_SEND_PULSE)
    url += '&auto';

  console.log(url);
  
  xhrRequest(url, 'GET', 
    function(responseText) {
      console.log("response:"+responseText);
      
      if(requestCode == JS_GET_STATS) {
        if(devMode) {
          returnStatus(JS_STATUS_STATS_OK, "ok,123,999");
        } else {
            returnStatus(JS_STATUS_STATS_OK, responseText);
        }
      } else if(requestCode == JS_SEND_PULSE){
        if(devMode) {
          returnStatus(JS_STATUS_PULSE_OK, "ok");
        } else {        
          if(responseText.substring(0,2) === "ok")
            returnStatus(JS_STATUS_PULSE_OK, "ok");
          else
            returnStatus(JS_STATUS_PULSE_OK, "invalid response from server");
        }
      }
    }      
  );
}

function sendPulse() {
  getLocation();
}
function getStats() {
  //setTimeout(function(){ getLocation(); }, 15000);

  getLocation();
}

function getMap() {
  // Construct URL
  var url = 'http://darkestmon.100webspace.net/pebble/pia.php?act=getMap';
  console.log(url);
  
  xhrRequest(url, 'GET', 
    function(responseText) {
      console.log("response:"+responseText);
      
      if(devMode) {
        //responseText="ok,nFLqCZhCFhCFnWzeGSnVSqxYkpEpyzeGSUGVnFnUGzm1/pysmX4oTfpyxojcpyppTepyrnWenG3m1+pjcVE7qR6ojaqCYn0UnU8pEOpjSn0OXttqhinFkm3Dpy9b5vXeFs9eqCpoTynVQqCbhCFoEEqCbdnEl3BnFnmWCqSFojGoFVjqimWso0cnkxUV8nGwUWbnG3nGwqCSnUsm3Mok2qR6lrKoVIkZqkKCk40nFpXtxpysoTyU1Ho0ho0fpyypEFlnOmKgqR5qCTqhhn1xTX7zUEzUEbK/o0fivWqR5mHFnkvqR7ivWpD+mHEpyrmGzpjTqCYVE7nFkmX4o0XqCYXttWyVnVSpEIok4zUErwFmmLeGSo0do0fo0gpTlnFpzUEgyJqCZtN5lnOivWmHFzUEm1/n1qzUEnFpb5vzUEpy3pEHzUEkKBfUWoipeGSqhuzUEVE7pyyqCbrAxm1/oT8oDgnFpn0bnWYok4pTdm1/nksn0lnVSnGwVE7ivWrgtzUEn0lpEFln2rv8nFpmWCoTednEhCFzUEnVSzUEpB6nVSzUEr/tqSGzUEokzkpCqCZpjYkpQo0dn0ckZqzUEzUEzUEzUEicTnVHmlqoD5XeFm1/rCRqR7mKgrBazUEXAFkpCnkkqCapjMUGVnVHoTfpyqzUEzUEpEHzUEqSJqTBeiyzUEnknn0ejq0zUEzUEnl/zUElnrzUEzUElX7giMmmOpyxpjAm5Zn1xo0hnksnFcqCZpjNrAvnkszUEe1YzUEr/tzUErAxzUEqCSoDkpjMzUEgvNnkUoEEnFplnwpyvdGxqxFzUEn0lzUEzUEqCZzUEm1SUGVUGVnFdqAvWAI";
        responseText="ok,dV0nVQm1/hCFnFLoD5pB6pyzVE7WAIqCZdV0UGVdV0";
      }
      
      if(responseText.substring(0,2) === "ok"){
        mapData= responseText.substring(3);
        //console.log("map data: "+mapData);
        mapPartCtr=0;
        returnStatus(JS_STATUS_MAP_OK, "ok");
      } else {       
        returnStatus(JS_STATUS_MAP_OK, "nok");
      }
    }      
  );
}

function getNextMap(){
  const messageSize=900;
  if(mapPartCtr<mapData.length) {
    var mapChunk =  mapData.substring(mapPartCtr, mapPartCtr+messageSize);
    console.log("mapPartCtr:"+mapPartCtr+"      mapChunk..."+mapChunk);
    mapPartCtr+=messageSize;
    returnStatus(JS_STATUS_NEXT_MAP_OK,mapChunk);
  } else {
    console.log("no more map chunk!");
    returnStatus(JS_STATUS_NEXT_MAP_OK, "@END");
  }
}

function getLocation() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    {timeout: 3000, maximumAge: Infinity}
  );
}


function strip(html)
{
  var stripped = html.replace(/<\/?.+?>/ig, '').split("\n\n").join("\n");
  console.log("stripped="+stripped);
   return stripped;
}


function returnStatus(code, data) {
  var retries=3;
  if(arguments[2]>=0)
    retries=arguments[2];
  
  if(code==JS_STATUS_STATS_OK) {    
    console.log("data.substring(0,2)...."+data.substring(0,2));
    if(data.substring(0,2) === "ok") {
      var splittedData = data.substring(3).split(",");
      console.log("hello data........................................");
      console.log(splittedData[0]);
      console.log(splittedData[1]);
      var dictionary = {
        'STATUS': code,
        'DATA1': splittedData[0],
        'DATA2': splittedData[1]
      };
    } else {
      
      var dictionary = {
        'STATUS': code,
        'DATA1': "nok",
        'DATA2': "\n\n"+strip(data).substring(0,80)
      };
    }
  } else {
    var dictionary = {
      'STATUS': code,
      'DATA1': data
    };
  }
  // Send to Pebble  
  Pebble.sendAppMessage(dictionary,
                        function(e) {
                          console.log('info sent to Pebble successfully!'+code);
                        },
                        function(e) {
                          console.log('Error sending info to Pebble!'+code);
                          if(retries>0){
                            retries--;
                            setTimeout( function(){
                              console.log("Retrying..."+ retries + " more try left");
                              returnStatus(code, data, retries);
                            }, 3000);
                          }
                        }
                       );
}

// Listen for when the watchface is opened
Pebble.addEventListener('ready', 
  function(e) {
    console.log('PebbleKit JS ready!');
    returnStatus(JS_STATUS_READY, "", 10);
  }
);