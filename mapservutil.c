/******************************************************************************
 * $id$
 *
 * Project:  MapServer
 * Purpose:  MapServer CGI utility functions.
 * Author:   Steve Lime and the MapServer team.
 *
 ******************************************************************************
 * Copyright (c) 1996-2005 Regents of the University of Minnesota.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies of this Software or works derived from this Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/


#include "mapserver.h"
#include "mapserv.h"
#include "maptime.h"
#include <sys/stat.h>

/*
** Enumerated types, keep the query modes in sequence and at the end of the enumeration (mode enumeration is in maptemplate.h).
*/
static int numModes = 21;
static char *modeStrings[21] = {"BROWSE","ZOOMIN","ZOOMOUT","MAP","LEGEND","LEGENDICON","REFERENCE","SCALEBAR","COORDINATE",
                                "QUERY","NQUERY","ITEMQUERY","ITEMNQUERY",
				"FEATUREQUERY","FEATURENQUERY","ITEMFEATUREQUERY","ITEMFEATURENQUERY",
				"INDEXQUERY","TILE","OWS", "WFS"};



int msCGIWriteLog(mapservObj *mapserv, int show_error)
{
  FILE *stream;
  int i;
  time_t t;
  char szPath[MS_MAXPATHLEN];

  if(!mapserv) return(MS_SUCCESS);
  if(!mapserv->map) return(MS_SUCCESS);
  if(!mapserv->map->web.log) return(MS_SUCCESS);
  
  if((stream = fopen(msBuildPath(szPath, mapserv->map->mappath, 
                                   mapserv->map->web.log),"a")) == NULL) {
    msSetError(MS_IOERR, mapserv->map->web.log, "msCGIWriteLog()");
    return(MS_FAILURE);
  }

  t = time(NULL);
  fprintf(stream,"%s,",msStringChop(ctime(&t)));
  fprintf(stream,"%d,",(int)getpid());
  
  if(getenv("REMOTE_ADDR") != NULL)
    fprintf(stream,"%s,",getenv("REMOTE_ADDR"));
  else
    fprintf(stream,"NULL,");
 
  fprintf(stream,"%s,",mapserv->map->name);
  fprintf(stream,"%d,",mapserv->Mode);

  fprintf(stream,"%f %f %f %f,", mapserv->map->extent.minx, mapserv->map->extent.miny, mapserv->map->extent.maxx, mapserv->map->extent.maxy);

  fprintf(stream,"%f %f,", mapserv->mappnt.x, mapserv->mappnt.y);

  for(i=0;i<mapserv->NumLayers;i++)
    fprintf(stream, "%s ", mapserv->Layers[i]);
  fprintf(stream,",");

  if(show_error == MS_TRUE)
    msWriteError(stream);
  else
    fprintf(stream, "normal execution");

  fprintf(stream,"\n");

  fclose(stream);
  return(MS_SUCCESS);
}

void msCGIWriteError(mapservObj *mapserv)
{
  errorObj *ms_error = msGetErrorObj();
  
  if(!ms_error || ms_error->code == MS_NOERR || ms_error->isreported) {
     /* either we have no error, or it was already reported by other means */
     return;
  }

  msCGIWriteLog(mapserv,MS_TRUE);

  if(!mapserv || !mapserv->map) {
    msIO_setHeader("Content-type","text/html");
    msIO_sendHeaders();
    msIO_printf("<HTML>\n");
    msIO_printf("<HEAD><TITLE>MapServer Message</TITLE></HEAD>\n");
    msIO_printf("<!-- %s -->\n", msGetVersion());
    msIO_printf("<BODY BGCOLOR=\"#FFFFFF\">\n");
    msWriteErrorXML(stdout);
    msIO_printf("</BODY></HTML>");
    return;
  }

  if((ms_error->code == MS_NOTFOUND) && (mapserv->map->web.empty)) {
    /* msRedirect(mapserv->map->web.empty); */
    if(msReturnURL(mapserv, mapserv->map->web.empty, BROWSE) != MS_SUCCESS) {
      msIO_setHeader("Content-type","text/html");
      msIO_sendHeaders();
      msIO_printf("<HTML>\n");
      msIO_printf("<HEAD><TITLE>MapServer Message</TITLE></HEAD>\n");
      msIO_printf("<!-- %s -->\n", msGetVersion());
      msIO_printf("<BODY BGCOLOR=\"#FFFFFF\">\n");
      msWriteErrorXML(stdout);
      msIO_printf("</BODY></HTML>");
    }
  } else {
    if(mapserv->map->web.error) {      
      /* msRedirect(mapserv->map->web.error); */
      if(msReturnURL(mapserv, mapserv->map->web.error, BROWSE) != MS_SUCCESS) {
        msIO_setHeader("Content-type","text/html");
        msIO_sendHeaders();
        msIO_printf("<HTML>\n");
        msIO_printf("<HEAD><TITLE>MapServer Message</TITLE></HEAD>\n");
        msIO_printf("<!-- %s -->\n", msGetVersion());
        msIO_printf("<BODY BGCOLOR=\"#FFFFFF\">\n");
        msWriteErrorXML(stdout);
        msIO_printf("</BODY></HTML>");
      }
    } else {
      msIO_setHeader("Content-type","text/html");
      msIO_sendHeaders();
      msIO_printf("<HTML>\n");
      msIO_printf("<HEAD><TITLE>MapServer Message</TITLE></HEAD>\n");
      msIO_printf("<!-- %s -->\n", msGetVersion());
      msIO_printf("<BODY BGCOLOR=\"#FFFFFF\">\n");
      msWriteErrorXML(stdout);
      msIO_printf("</BODY></HTML>");
    }
  }
}

/*
** Converts a string (e.g. form parameter) to a double, first checking the format against
** a regular expression. returns an error if the format test fails.
*/

#define GET_NUMERIC(string,dbl) do { \
  dbl = strtod((string), &strtoderr);\
  if (*strtoderr) {\
    msSetError(MS_TYPEERR, NULL, "GET_NUMERIC()");\
    return MS_FAILURE;\
  }\
} while (0)



/*
** Extract Map File name from params and load it.  
** Returns map object or NULL on error.
*/
mapObj *msCGILoadMap(mapservObj *mapserv)
{
  int i;
  mapObj *map = NULL;
  static mapObj *preloadedmap = NULL;
  static struct timespec preloadedmap_mtime;
  static char *ms_mapfile = NULL;
  struct stat mapfile_stat;

  /* check if we should use and/or create a pre-parsed mapfile */
  ms_mapfile = getenv("MS_MAPFILE");
  if(preloadedmap || ms_mapfile) {
     if(preloadedmap) {
        /* we already have a preloaded mapfile, check if the mapfile itself hasn't changed */
        stat(ms_mapfile,&mapfile_stat);
        if(mapfile_stat.st_mtimespec.tv_sec > preloadedmap_mtime.tv_sec) {
           /* the mapfile has been updated on disk, discard the cached mapObj */
           msFreeMap(preloadedmap);
           preloadedmap = NULL;
           msDebug("reloading mapfile %s as it has been changed on disk",ms_mapfile);
        }
     }
     if(!preloadedmap) {
        /* either the mapfile has never been loaded, or it has been destroyed because it was outdated */
        preloadedmap = msLoadMap(ms_mapfile,NULL);
        if(!preloadedmap) return NULL;
        stat(ms_mapfile,&mapfile_stat);
        preloadedmap_mtime = mapfile_stat.st_mtimespec;
     }
     map = msNewMapObj();
     msCopyMap(map,preloadedmap);
  } else {
     for(i=0;i<mapserv->request->NumParams;i++) /* find the mapfile parameter first */
        if(strcasecmp(mapserv->request->ParamNames[i], "map") == 0) break;

     if(i == mapserv->request->NumParams) {
        msSetError(MS_WEBERR, "CGI variable \"map\" is not set.", "msCGILoadMap()"); /* no default, outta here */
        return NULL;
     } else {
        if(getenv(mapserv->request->ParamValues[i])) /* an environment variable references the actual file to use */
           map = msLoadMap(getenv(mapserv->request->ParamValues[i]), NULL);
        else {
           /* by here we know the request isn't for something in an environment variable */
           if(getenv("MS_MAP_NO_PATH")) {
              msSetError(MS_WEBERR, "Mapfile not found in environment variables and this server is not configured for full paths.", "msCGILoadMap()");
              return NULL;
           }

           if(getenv("MS_MAP_PATTERN") && msEvalRegex(getenv("MS_MAP_PATTERN"), mapserv->request->ParamValues[i]) != MS_TRUE) {
              msSetError(MS_WEBERR, "Parameter 'map' value fails to validate.", "msCGILoadMap()");
              return NULL;
           }

           /* ok to try to load now */
           map = msLoadMap(mapserv->request->ParamValues[i], NULL);
        }
     }
  }

  if(!map) return NULL;

  /* check for any %variable% substitutions here, also do any map_ changes, we do this here so WMS/WFS  */
  /* services can take advantage of these "vendor specific" extensions */
  for(i=0;i<mapserv->request->NumParams;i++) {
    /*
    ** a few CGI variables should be skipped altogether
    **
    ** qstring: there is separate per layer validation for attribute queries and the substitution checks
    **          below conflict with that so we avoid it here
    */
    if(strncasecmp(mapserv->request->ParamNames[i],"qstring",7) == 0) continue;

    if(strncasecmp(mapserv->request->ParamNames[i],"map_",4) == 0 || strncasecmp(mapserv->request->ParamNames[i],"map.",4) == 0) { /* check to see if there are any additions to the mapfile */
      if(msUpdateMapFromURL(map, mapserv->request->ParamNames[i], mapserv->request->ParamValues[i]) != MS_SUCCESS) {
         msFreeMap(map);
         return NULL;
      }
      continue;
    }
  }

  msApplySubstitutions(map, mapserv->request->ParamNames, mapserv->request->ParamValues, mapserv->request->NumParams);
  msApplyDefaultSubstitutions(map);

  /* check to see if a ogc map context is passed as argument. if there */
  /* is one load it */

  for(i=0;i<mapserv->request->NumParams;i++) {
    if(strcasecmp(mapserv->request->ParamNames[i],"context") == 0) {
      if(mapserv->request->ParamValues[i] && strlen(mapserv->request->ParamValues[i]) > 0) {
        if(strncasecmp(mapserv->request->ParamValues[i],"http",4) == 0) {
          if(msGetConfigOption(map, "CGI_CONTEXT_URL"))
            msLoadMapContextURL(map, mapserv->request->ParamValues[i], MS_FALSE);
        } else
            msLoadMapContext(map, mapserv->request->ParamValues[i], MS_FALSE); 
      }
    }
  } 
  /*
   * RFC-42 HTTP Cookie Forwarding
   * Here we set the http_cookie_data metadata to handle the 
   * HTTP Cookie Forwarding. The content of this metadata is the cookie 
   * content. In the future, this metadata will probably be replaced
   * by an object that is part of the mapObject that would contain 
   * information on the application status (such as cookie).
   */
  if( mapserv->request->httpcookiedata != NULL )
  {
     msInsertHashTable( &(map->web.metadata), "http_cookie_data",
           mapserv->request->httpcookiedata );
  }

  return map;
}


/*
** Set operation mode. First look in MS_MODE env. var. as a
** default value that can be overridden by the mode=... CGI param.
** Returns silently, leaving mapserv->Mode unchanged if mode param not set.
*/
int msCGISetMode(mapservObj *mapserv)
{
    const char *mode = NULL;
    int i, j;


    mode = getenv("MS_MODE");
    for( i=0; i<mapserv->request->NumParams; i++ ) 
    {
        if(strcasecmp(mapserv->request->ParamNames[i], "mode") == 0)
        {
            mode = mapserv->request->ParamValues[i];
            break;
        }
    }

    if (mode) {
      for(j=0; j<numModes; j++) {
        if(strcasecmp(mode, modeStrings[j]) == 0) {
          mapserv->Mode = j;
          break;
        }
      }

      if(j == numModes) {
        msSetError(MS_WEBERR, "Invalid mode.", "msCGISetMode()");
        return MS_FAILURE;
      }
    }

    return MS_SUCCESS;
}




/*
** Process CGI parameters.
*/
int msCGILoadForm(mapservObj *mapserv)
{
  int i,n;
  char **tokens=NULL;
  int rosa_type=0;
  double tmpval;
  char *strtoderr;

  for(i=0;i<mapserv->request->NumParams;i++) { /* now process the rest of the form variables */
    if(strlen(mapserv->request->ParamValues[i]) == 0)
      continue;
    
    
    if(strcasecmp(mapserv->request->ParamNames[i],"icon") == 0) {      
      mapserv->icon = msStrdup(mapserv->request->ParamValues[i]);
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"queryfile") == 0) {      
      mapserv->QueryFile = msStrdup(mapserv->request->ParamValues[i]);
      continue;
    }
    
    if(strcasecmp(mapserv->request->ParamNames[i],"savequery") == 0) {
      mapserv->savequery = MS_TRUE;
      continue;
    }
    
    /* Insecure as implemented, need to save someplace non accessible by everyone in the universe
        if(strcasecmp(mapserv->request->ParamNames[i],"savemap") == 0) {      
         mapserv->savemap = MS_TRUE;
         continue;
        }
    */

    if(strcasecmp(mapserv->request->ParamNames[i],"zoom") == 0) {
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->Zoom);
      if((mapserv->Zoom > MAXZOOM) || (mapserv->Zoom < MINZOOM)) {
        msSetError(MS_WEBERR, "Zoom value out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"zoomdir") == 0) {
      GET_NUMERIC(mapserv->request->ParamValues[i],tmpval);
      mapserv->ZoomDirection = (int)tmpval;
      if((mapserv->ZoomDirection != -1) && (mapserv->ZoomDirection != 1) && (mapserv->ZoomDirection != 0)) {
        msSetError(MS_WEBERR, "Zoom direction must be 1, 0 or -1.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"zoomsize") == 0) { /* absolute zoom magnitude */
      GET_NUMERIC(mapserv->request->ParamValues[i],tmpval);
      mapserv->ZoomSize = (int)tmpval;
      if((mapserv->ZoomSize > MAXZOOM) || (mapserv->ZoomSize < 1)) {
        msSetError(MS_WEBERR, "Invalid zoom size.", "msCGILoadForm()");
        return MS_FAILURE;
      }    
      continue;
    }
    
    if(strcasecmp(mapserv->request->ParamNames[i],"imgext") == 0) { /* extent of an existing image in a web application */
      tokens = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);

      if(!tokens) {
        msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
        return MS_FAILURE;
      }

      if(n != 4) {
        msSetError(MS_WEBERR, "Not enough arguments for imgext.", "msCGILoadForm()");
        return MS_FAILURE;
      }

      GET_NUMERIC(tokens[0],mapserv->ImgExt.minx);
      GET_NUMERIC(tokens[1],mapserv->ImgExt.miny);
      GET_NUMERIC(tokens[2],mapserv->ImgExt.maxx);
      GET_NUMERIC(tokens[3],mapserv->ImgExt.maxy);

      msFreeCharArray(tokens, 4);
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"searchmap") == 0) {      
      mapserv->SearchMap = MS_TRUE;
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"id") == 0) {
      if(msEvalRegex(IDPATTERN, mapserv->request->ParamValues[i]) == MS_FALSE) { 
	      msSetError(MS_WEBERR, "Parameter 'id' value fails to validate.", "msCGILoadForm()");
         return MS_FAILURE;
      }
      strlcpy(mapserv->Id, mapserv->request->ParamValues[i], IDSIZE);
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"mapext") == 0) { /* extent of the new map or query */

      if(strncasecmp(mapserv->request->ParamValues[i],"shape",5) == 0)
        mapserv->UseShapes = MS_TRUE;
      else {
        tokens = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);
    
        if(!tokens) {
          msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
          return MS_FAILURE;
        }
    
        if(n != 4) {
          msSetError(MS_WEBERR, "Not enough arguments for mapext.", "msCGILoadForm()");
          return MS_FAILURE;
        }
    
         GET_NUMERIC(tokens[0],mapserv->map->extent.minx);
         GET_NUMERIC(tokens[1],mapserv->map->extent.miny);
         GET_NUMERIC(tokens[2],mapserv->map->extent.maxx);
         GET_NUMERIC(tokens[3],mapserv->map->extent.maxy);    
    
        msFreeCharArray(tokens, 4);
    
#ifdef USE_PROJ
        /* 
         * If there is a projection in the map file, and it is not lon/lat, and the 
         * extents "look like" they *are* lon/lat, based on their size,
         * then convert the extents to the map file projection.
         *
         * DANGER: If the extents are legitimately in the mapfile projection
         *         and coincidentally fall in the lon/lat range, bad things
         *         will ensue.
         */
        if(mapserv->map->projection.proj && !pj_is_latlong(mapserv->map->projection.proj)
           && (mapserv->map->extent.minx >= -180.0 && mapserv->map->extent.minx <= 180.0) 
           && (mapserv->map->extent.miny >= -90.0 && mapserv->map->extent.miny <= 90.0)
           && (mapserv->map->extent.maxx >= -180.0 && mapserv->map->extent.maxx <= 180.0) 
           && (mapserv->map->extent.maxy >= -90.0 && mapserv->map->extent.maxy <= 90.0)) {
          msProjectRect(&(mapserv->map->latlon), &(mapserv->map->projection), &(mapserv->map->extent)); /* extent is a in lat/lon */
        }
#endif

        if((mapserv->map->extent.minx != mapserv->map->extent.maxx) && (mapserv->map->extent.miny != mapserv->map->extent.maxy)) { /* extent seems ok */
          mapserv->CoordSource = FROMUSERBOX;
          mapserv->QueryCoordSource = FROMUSERBOX;
        }
      }

      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"minx") == 0) { /* extent of the new map, in pieces */
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->map->extent.minx);      
      continue;
    }
    if(strcasecmp(mapserv->request->ParamNames[i],"maxx") == 0) {      
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->map->extent.maxx);      
      continue;
    }
    if(strcasecmp(mapserv->request->ParamNames[i],"miny") == 0) {
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->map->extent.miny);      
      continue;
    }
    if(strcasecmp(mapserv->request->ParamNames[i],"maxy") == 0) {
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->map->extent.maxy);      
      mapserv->CoordSource = FROMUSERBOX;
      mapserv->QueryCoordSource = FROMUSERBOX;
      continue;
    } 

    if(strcasecmp(mapserv->request->ParamNames[i],"mapxy") == 0) { /* user map coordinate */
      
      if(strncasecmp(mapserv->request->ParamValues[i],"shape",5) == 0) {
        mapserv->UseShapes = MS_TRUE;    
      } else {
        tokens = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);

        if(!tokens) {
          msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
          return MS_FAILURE;
        }
    
        if(n != 2) {
          msSetError(MS_WEBERR, "Not enough arguments for mapxy.", "msCGILoadForm()");
          return MS_FAILURE;
        }
    
        GET_NUMERIC(tokens[0],mapserv->mappnt.x );
        GET_NUMERIC(tokens[1],mapserv->mappnt.y );
    
        msFreeCharArray(tokens, 2);

#ifdef USE_PROJ
        if(mapserv->map->projection.proj && !pj_is_latlong(mapserv->map->projection.proj)
           && (mapserv->mappnt.x >= -180.0 && mapserv->mappnt.x <= 180.0) 
           && (mapserv->mappnt.y >= -90.0 && mapserv->mappnt.y <= 90.0)) {
          msProjectPoint(&(mapserv->map->latlon), &(mapserv->map->projection), &mapserv->mappnt); /* point is a in lat/lon */
        }
#endif

        if(mapserv->CoordSource == NONE) { /* don't override previous settings (i.e. buffer or scale ) */
          mapserv->CoordSource = FROMUSERPNT;
          mapserv->QueryCoordSource = FROMUSERPNT;
        }
      }
      continue;
    }

    /*
    ** Query shape consisting of map or image coordinates. It's almost identical processing so we'll do either in this block...
    */
    if(strcasecmp(mapserv->request->ParamNames[i], "mapshape") == 0 || strcasecmp(mapserv->request->ParamNames[i], "imgshape") == 0) {
      if(strcasecmp(mapserv->request->ParamNames[i],"mapshape") == 0)
        mapserv->QueryCoordSource = FROMUSERSHAPE;
      else
        mapserv->QueryCoordSource = FROMIMGSHAPE;

      if(strchr(mapserv->request->ParamValues[i], '(') != NULL) { /* try WKT */
        if((mapserv->map->query.shape = msShapeFromWKT(mapserv->request->ParamValues[i])) == NULL) {
          msSetError(MS_WEBERR, "WKT parse failed for mapshape/imgshape.", "msCGILoadForm()");
          return MS_FAILURE;
        }
      } else {
        lineObj line={0,NULL};
        char **tmp=NULL;
        int n, j;
      
        tmp = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);

        if(n%2 != 0 || n<8) { /* n must be even and be at least 8 */
          msSetError(MS_WEBERR, "Malformed polygon geometry for mapshape/imgshape.", "msCGILoadForm()");
          return MS_FAILURE;
        }

        line.numpoints = n/2;
        if((line.point = (pointObj *)malloc(sizeof(pointObj)*line.numpoints)) == NULL) {
          msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
          return MS_FAILURE;
        }

        if((mapserv->map->query.shape = (shapeObj *) malloc(sizeof(shapeObj))) == NULL) {
          msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
          return MS_FAILURE;
        }
        msInitShape(mapserv->map->query.shape);
        mapserv->map->query.shape->type = MS_SHAPE_POLYGON;

        for(j=0; j<line.numpoints; j++) {
          line.point[j].x = atof(tmp[2*j]);
          line.point[j].y = atof(tmp[2*j+1]);

#ifdef USE_PROJ
          if(mapserv->QueryCoordSource == FROMUSERSHAPE && mapserv->map->projection.proj && !pj_is_latlong(mapserv->map->projection.proj)
             && (line.point[j].x >= -180.0 && line.point[j].x <= 180.0) 
             && (line.point[j].y >= -90.0 && line.point[j].y <= 90.0)) {
            msProjectPoint(&(mapserv->map->latlon), &(mapserv->map->projection), &line.point[j]); /* point is a in lat/lon */
          }
#endif
        }

        if(msAddLine(mapserv->map->query.shape, &line) == -1) {
           msFree(line.point);
           msFreeCharArray(tmp, n);
           return MS_FAILURE;
        }

        msFree(line.point);
        msFreeCharArray(tmp, n);
      }

      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"img.x") == 0) { /* mouse click, in pieces */
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->ImgPnt.x);
      if((mapserv->ImgPnt.x > (2*mapserv->map->maxsize)) || (mapserv->ImgPnt.x < (-2*mapserv->map->maxsize))) {
        msSetError(MS_WEBERR, "Coordinate out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      mapserv->CoordSource = FROMIMGPNT;
      mapserv->QueryCoordSource = FROMIMGPNT;
      continue;
    }
    if(strcasecmp(mapserv->request->ParamNames[i],"img.y") == 0) {
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->ImgPnt.y);
      if((mapserv->ImgPnt.y > (2*mapserv->map->maxsize)) || (mapserv->ImgPnt.y < (-2*mapserv->map->maxsize))) {
        msSetError(MS_WEBERR, "Coordinate out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      mapserv->CoordSource = FROMIMGPNT;
      mapserv->QueryCoordSource = FROMIMGPNT;
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"imgxy") == 0) { /* mouse click, single variable */
      if(mapserv->CoordSource == FROMIMGPNT)
        continue;

      tokens = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);

      if(!tokens) {
        msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
        return MS_FAILURE;
      }

      if(n != 2) {
        msSetError(MS_WEBERR, "Not enough arguments for imgxy.", "msCGILoadForm()");
        return MS_FAILURE;
      }

      GET_NUMERIC(tokens[0],mapserv->ImgPnt.x );
      GET_NUMERIC(tokens[1],mapserv->ImgPnt.y );

      msFreeCharArray(tokens, 2);

      if((mapserv->ImgPnt.x > (2*mapserv->map->maxsize)) || (mapserv->ImgPnt.x < (-2*mapserv->map->maxsize)) || (mapserv->ImgPnt.y > (2*mapserv->map->maxsize)) || (mapserv->ImgPnt.y < (-2*mapserv->map->maxsize))) {
        msSetError(MS_WEBERR, "Reference map coordinate out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }

      if(mapserv->CoordSource == NONE) { /* override nothing since this parameter is usually used to hold a default value */
        mapserv->CoordSource = FROMIMGPNT;
        mapserv->QueryCoordSource = FROMIMGPNT;
      }
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"imgbox") == 0) { /* selection box (eg. mouse drag) */
      tokens = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);
      
      if(!tokens) {
        msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
        return MS_FAILURE;
      }
      
      if(n != 4) {
        msSetError(MS_WEBERR, "Not enough arguments for imgbox.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      
      GET_NUMERIC(tokens[0],mapserv->ImgBox.minx);
      GET_NUMERIC(tokens[1],mapserv->ImgBox.miny);
      GET_NUMERIC(tokens[2],mapserv->ImgBox.maxx);
      GET_NUMERIC(tokens[3],mapserv->ImgBox.maxy);
      
      msFreeCharArray(tokens, 4);

      if((mapserv->ImgBox.minx != mapserv->ImgBox.maxx) && (mapserv->ImgBox.miny != mapserv->ImgBox.maxy)) { /* must not degenerate into a point */
        mapserv->CoordSource = FROMIMGBOX;
        mapserv->QueryCoordSource = FROMIMGBOX;
      }
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"ref.x") == 0) { /* mouse click in reference image, in pieces */
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->RefPnt.x);
      if((mapserv->RefPnt.x > (2*mapserv->map->maxsize)) || (mapserv->RefPnt.x < (-2*mapserv->map->maxsize))) {
        msSetError(MS_WEBERR, "Coordinate out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      mapserv->CoordSource = FROMREFPNT;
      continue;
    }
    if(strcasecmp(mapserv->request->ParamNames[i],"ref.y") == 0) {
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->RefPnt.y);
      if((mapserv->RefPnt.y > (2*mapserv->map->maxsize)) || (mapserv->RefPnt.y < (-2*mapserv->map->maxsize))) {
        msSetError(MS_WEBERR, "Coordinate out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      mapserv->CoordSource = FROMREFPNT;
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"refxy") == 0) { /* mouse click in reference image, single variable */
      tokens = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);

      if(!tokens) {
        msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
        return MS_FAILURE;
      }

      if(n != 2) {
        msSetError(MS_WEBERR, "Not enough arguments for imgxy.", "msCGILoadForm()");
        return MS_FAILURE;
      }

      GET_NUMERIC(tokens[0],mapserv->RefPnt.x);
      GET_NUMERIC(tokens[1],mapserv->RefPnt.y);

      msFreeCharArray(tokens, 2);
      
      if((mapserv->RefPnt.x > (2*mapserv->map->maxsize)) || (mapserv->RefPnt.x < (-2*mapserv->map->maxsize)) || (mapserv->RefPnt.y > (2*mapserv->map->maxsize)) || (mapserv->RefPnt.y < (-2*mapserv->map->maxsize))) {
        msSetError(MS_WEBERR, "Reference map coordinate out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      
      mapserv->CoordSource = FROMREFPNT;
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"buffer") == 0) { /* radius (map units), actually 1/2 square side */
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->Buffer);
      mapserv->CoordSource = FROMBUF;
      mapserv->QueryCoordSource = FROMUSERPNT;
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"scale") == 0 || strcasecmp(mapserv->request->ParamNames[i],"scaledenom") == 0) { /* scale for new map */
      GET_NUMERIC(mapserv->request->ParamValues[i],mapserv->ScaleDenom);
      if(mapserv->ScaleDenom <= 0) {
        msSetError(MS_WEBERR, "Scale out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      mapserv->CoordSource = FROMSCALE;
      mapserv->QueryCoordSource = FROMUSERPNT;
      continue;
    }
    
    if(strcasecmp(mapserv->request->ParamNames[i],"imgsize") == 0) { /* size of existing image (pixels) */
      tokens = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);

      if(!tokens) {
        msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
        return MS_FAILURE;
      }

      if(n != 2) {
        msSetError(MS_WEBERR, "Not enough arguments for imgsize.", "msCGILoadForm()");
        return MS_FAILURE;
      }

      GET_NUMERIC(tokens[0],tmpval);
      mapserv->ImgCols = (int)tmpval;
      GET_NUMERIC(tokens[1],tmpval);
      mapserv->ImgRows = (int)tmpval;

      msFreeCharArray(tokens, 2);
      
      if(mapserv->ImgCols > mapserv->map->maxsize || mapserv->ImgRows > mapserv->map->maxsize || mapserv->ImgCols <= 0 || mapserv->ImgRows <= 0) {
        msSetError(MS_WEBERR, "Image size out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
 
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"mapsize") == 0) { /* size of new map (pixels) */
      tokens = msStringSplit(mapserv->request->ParamValues[i], ' ', &n);

      if(!tokens) {
        msSetError(MS_MEMERR, NULL, "msCGILoadForm()");
        return MS_FAILURE;
      }

      if(n != 2) {
        msSetError(MS_WEBERR, "Not enough arguments for mapsize.", "msCGILoadForm()");
        return MS_FAILURE;
      }

      GET_NUMERIC(tokens[0],tmpval);
      mapserv->map->width = (int)tmpval;
      GET_NUMERIC(tokens[1],tmpval);
      mapserv->map->height = (int)tmpval;

      msFreeCharArray(tokens, 2);
      
      if(mapserv->map->width > mapserv->map->maxsize || mapserv->map->height > mapserv->map->maxsize || mapserv->map->width <= 0 || mapserv->map->height <= 0) {
        msSetError(MS_WEBERR, "Image size out of range.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      continue;
    }

    if(strncasecmp(mapserv->request->ParamNames[i],"layers", 6) == 0) { /* turn a set of layers, delimited by spaces, on */

      /* If layers=all then turn on all layers */
      if (strcasecmp(mapserv->request->ParamValues[i], "all") == 0 && mapserv->map != NULL) {
        int l;

        /* Reset NumLayers=0. If individual layers were already selected then free the previous values.  */
        for(l=0; l<mapserv->NumLayers; l++)
          msFree(mapserv->Layers[l]);
        mapserv->NumLayers=0;

        for(mapserv->NumLayers=0; mapserv->NumLayers < mapserv->map->numlayers; mapserv->NumLayers++) {
          if(msGrowMapservLayers(mapserv) == MS_FAILURE)
            return MS_FAILURE;

          if(GET_LAYER(mapserv->map, mapserv->NumLayers)->name) {
            mapserv->Layers[mapserv->NumLayers] = msStrdup(GET_LAYER(mapserv->map, mapserv->NumLayers)->name);
          } else {
            mapserv->Layers[mapserv->NumLayers] = msStrdup("");
          }
        }
      } else {
        int num_layers=0, l;
        char **layers=NULL;

        layers = msStringSplit(mapserv->request->ParamValues[i], ' ', &(num_layers));
        for(l=0; l<num_layers; l++) {
          if(msGrowMapservLayers(mapserv) == MS_FAILURE)
            return MS_FAILURE;
          mapserv->Layers[mapserv->NumLayers++] = msStrdup(layers[l]);
        }

        msFreeCharArray(layers, num_layers);
        num_layers = 0;
      }

      continue;
    }

    if(strncasecmp(mapserv->request->ParamNames[i],"layer", 5) == 0) { /* turn a single layer/group on */
      if(msGrowMapservLayers(mapserv) == MS_FAILURE)
        return MS_FAILURE;
      mapserv->Layers[mapserv->NumLayers] = msStrdup(mapserv->request->ParamValues[i]);
      mapserv->NumLayers++;
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"qlayer") == 0) { /* layer to query (i.e search) */
      mapserv->QueryLayer = msStrdup(mapserv->request->ParamValues[i]);
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"qitem") == 0) { /* attribute to query on (optional) */
      mapserv->QueryItem = msStrdup(mapserv->request->ParamValues[i]);
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"qstring") == 0) { /* attribute query string */
      mapserv->QueryString = msStrdup(mapserv->request->ParamValues[i]);
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"qformat") == 0) { /* format to apply to query results (shortcut instead of having to use "map.web=QUERYFORMAT+foo") */
      if(mapserv->map->web.queryformat) free(mapserv->map->web.queryformat); /* avoid leak */
      mapserv->map->web.queryformat = msStrdup(mapserv->request->ParamValues[i]);
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"slayer") == 0) { /* layer to select (for feature based search) */
      mapserv->SelectLayer = msStrdup(mapserv->request->ParamValues[i]);
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"shapeindex") == 0) { /* used for index queries */
      GET_NUMERIC(mapserv->request->ParamValues[i],tmpval);
      mapserv->ShapeIndex = (int)tmpval;
      continue;
    }
    if(strcasecmp(mapserv->request->ParamNames[i],"tileindex") == 0) {
      GET_NUMERIC(mapserv->request->ParamValues[i],tmpval);
      mapserv->TileIndex = (int)tmpval;
      continue;
    }

    /* -------------------------------------------------------------------- 
     *   The following code is used to support mode=tile                    
     * -------------------------------------------------------------------- */ 

    if(strcasecmp(mapserv->request->ParamNames[i], "tilemode") == 0) { 
      /* currently, only valid tilemode is "spheremerc" */
      if( strcasecmp(mapserv->request->ParamValues[i], "gmap") == 0) {
        mapserv->TileMode = TILE_GMAP;
      } else if ( strcasecmp(mapserv->request->ParamValues[i], "ve") == 0 ) {
        mapserv->TileMode = TILE_VE;
      } else {
        msSetError(MS_WEBERR, "Invalid tilemode. Use one of: gmap, ve", "msCGILoadForm()");
        return MS_FAILURE;
      }
      continue;
    }

    if(strcasecmp(mapserv->request->ParamNames[i],"tile") == 0) { 

      if( strlen(mapserv->request->ParamValues[i]) < 1 ) {
        msSetError(MS_WEBERR, "Empty tile parameter.", "msCGILoadForm()");
        return MS_FAILURE;
      }
      mapserv->CoordSource = FROMTILE;
      mapserv->TileCoords = msStrdup(mapserv->request->ParamValues[i]);
      
      continue;
    }

    /* -------------------------------------------------------------------- */
    /*      The following code is used to support the rosa applet (for      */
    /*      more information on Rosa, please consult :                      */
    /*      http://www.maptools.org/rosa/) .                                */
    /*      This code was provided by Tim.Mackey@agso.gov.au.               */
    /*                                                                      */
    /*      For Application using it can be seen at :                       */    
    /*        http://www.agso.gov.au/map/pilbara/                           */
    /*                                                                      */
    /* -------------------------------------------------------------------- */

    if(strcasecmp(mapserv->request->ParamNames[i],"INPUT_TYPE") == 0)
    { /* Rosa input type */
        if(strcasecmp(mapserv->request->ParamValues[i],"auto_rect") == 0) 
        {
            rosa_type=1; /* rectangle */
            continue;
        }
            
        if(strcasecmp(mapserv->request->ParamValues[i],"auto_point") == 0) 
        {
            rosa_type=2; /* point */
            continue;
        }
    }
    if(strcasecmp(mapserv->request->ParamNames[i],"INPUT_COORD") == 0) 
    { /* Rosa coordinates */
 
       switch(rosa_type)
       {
         case 1:
             sscanf(mapserv->request->ParamValues[i],"%lf,%lf;%lf,%lf",
                    &mapserv->ImgBox.minx,&mapserv->ImgBox.miny,&mapserv->ImgBox.maxx,
                    &mapserv->ImgBox.maxy);
             if((mapserv->ImgBox.minx != mapserv->ImgBox.maxx) && 
                (mapserv->ImgBox.miny != mapserv->ImgBox.maxy)) 
             {
                 mapserv->CoordSource = FROMIMGBOX;
                 mapserv->QueryCoordSource = FROMIMGBOX;
             }
             else 
             {
                 mapserv->CoordSource = FROMIMGPNT;
                 mapserv->QueryCoordSource = FROMIMGPNT;
                 mapserv->ImgPnt.x=mapserv->ImgBox.minx;
                 mapserv->ImgPnt.y=mapserv->ImgBox.miny;
       }
           break;
         case 2:
           sscanf(mapserv->request->ParamValues[i],"%lf,%lf",&mapserv->ImgPnt.x,
                   &mapserv->ImgPnt.y);
           mapserv->CoordSource = FROMIMGPNT;
           mapserv->QueryCoordSource = FROMIMGPNT;
           break;
         }
       continue;
    }    
    /* -------------------------------------------------------------------- */
    /*      end of code for Rosa support.                                   */
    /* -------------------------------------------------------------------- */

  } /* next parameter */

  if(mapserv->Mode == ZOOMIN) {
    mapserv->ZoomDirection = 1;
    mapserv->Mode = BROWSE;
  }     
  if(mapserv->Mode == ZOOMOUT) {
    mapserv->ZoomDirection = -1;
    mapserv->Mode = BROWSE;
  }

  if(mapserv->ZoomSize != 0) { /* use direction and magnitude to calculate zoom */
    if(mapserv->ZoomDirection == 0) {
      mapserv->fZoom = 1;
    } else {
      mapserv->fZoom = mapserv->ZoomSize*mapserv->ZoomDirection;
      if(mapserv->fZoom < 0)
        mapserv->fZoom = 1.0/MS_ABS(mapserv->fZoom);
    }
  } else { /* use single value for zoom */
    if((mapserv->Zoom >= -1) && (mapserv->Zoom <= 1)) {
      mapserv->fZoom = 1; /* pan */
    } else {
      if(mapserv->Zoom < 0)
        mapserv->fZoom = 1.0/MS_ABS(mapserv->Zoom);
      else
        mapserv->fZoom = mapserv->Zoom;
    }
  }

  if(mapserv->ImgRows == -1) mapserv->ImgRows = mapserv->map->height;
  if(mapserv->ImgCols == -1) mapserv->ImgCols = mapserv->map->width;  
  if(mapserv->map->height == -1) mapserv->map->height = mapserv->ImgRows;
  if(mapserv->map->width == -1) mapserv->map->width = mapserv->ImgCols;  
  return MS_SUCCESS;
}

int setExtentFromShapes(mapservObj *mapserv) {
  int found=0;
  double dx, dy, cellsize;

  rectObj tmpext={-1.0,-1.0,-1.0,-1.0};
  pointObj tmppnt={-1.0,-1.0};

  found = msGetQueryResultBounds(mapserv->map, &(tmpext));

  dx = tmpext.maxx - tmpext.minx;
  dy = tmpext.maxy - tmpext.miny;
 
  tmppnt.x = (tmpext.maxx + tmpext.minx)/2;
  tmppnt.y = (tmpext.maxy + tmpext.miny)/2;
  tmpext.minx -= dx*EXTENT_PADDING/2.0;
  tmpext.maxx += dx*EXTENT_PADDING/2.0;
  tmpext.miny -= dy*EXTENT_PADDING/2.0;
  tmpext.maxy += dy*EXTENT_PADDING/2.0;

  if(mapserv->ScaleDenom != 0) { /* apply the scale around the center point (tmppnt) */
    cellsize = (mapserv->ScaleDenom/mapserv->map->resolution)/msInchesPerUnit(mapserv->map->units,0); /* user supplied a point and a scale */
    tmpext.minx = tmppnt.x - cellsize*mapserv->map->width/2.0;
    tmpext.miny = tmppnt.y - cellsize*mapserv->map->height/2.0;
    tmpext.maxx = tmppnt.x + cellsize*mapserv->map->width/2.0;
    tmpext.maxy = tmppnt.y + cellsize*mapserv->map->height/2.0;
  } else if(mapserv->Buffer != 0) { /* apply the buffer around the center point (tmppnt) */
    tmpext.minx = tmppnt.x - mapserv->Buffer;
    tmpext.miny = tmppnt.y - mapserv->Buffer;
    tmpext.maxx = tmppnt.x + mapserv->Buffer;
    tmpext.maxy = tmppnt.y + mapserv->Buffer;
  }

  /* in case we don't get  usable extent at this point (i.e. single point result) */
  if(!MS_VALID_EXTENT(tmpext)) {
    if(mapserv->map->web.minscaledenom > 0) { /* try web object minscale first */
      cellsize = (mapserv->map->web.minscaledenom/mapserv->map->resolution)/msInchesPerUnit(mapserv->map->units,0); /* user supplied a point and a scale */
      tmpext.minx = tmppnt.x - cellsize*mapserv->map->width/2.0;
      tmpext.miny = tmppnt.y - cellsize*mapserv->map->height/2.0;
      tmpext.maxx = tmppnt.x + cellsize*mapserv->map->width/2.0;
      tmpext.maxy = tmppnt.y + cellsize*mapserv->map->height/2.0;
    } else {
      msSetError(MS_WEBERR, "No way to generate a valid map extent from selected shapes.", "mapserv()");
      return MS_FAILURE;
    }
  }

  mapserv->mappnt = tmppnt;
  mapserv->map->extent = mapserv->RawExt = tmpext; /* save unadjusted extent */

  return MS_SUCCESS;
}


/* FIX: NEED ERROR CHECKING HERE FOR IMGPNT or MAPPNT */
void setCoordinate(mapservObj *mapserv)
{
  double cellx,celly;

  cellx = MS_CELLSIZE(mapserv->ImgExt.minx, mapserv->ImgExt.maxx, mapserv->ImgCols);
  celly = MS_CELLSIZE(mapserv->ImgExt.miny, mapserv->ImgExt.maxy, mapserv->ImgRows);

  mapserv->mappnt.x = MS_IMAGE2MAP_X(mapserv->ImgPnt.x, mapserv->ImgExt.minx, cellx);
  mapserv->mappnt.y = MS_IMAGE2MAP_Y(mapserv->ImgPnt.y, mapserv->ImgExt.maxy, celly);

  return;
}


int msCGIDispatchBrowseRequest(mapservObj *mapserv) {
   char *template =  NULL;
   int i,status;
   for(i=0;i<mapserv->request->NumParams;i++) /* find the template param value */
      if (strcasecmp(mapserv->request->ParamNames[i], "template") == 0)
         template = mapserv->request->ParamValues[i];

   if ( (!mapserv->map->web.template) && (template==NULL || (strcasecmp(template, "openlayers")!=0)) ) {
      msSetError(MS_WEBERR, "Traditional BROWSE mode requires a TEMPLATE in the WEB section, but none was provided.", "mapserv()");
      return MS_FAILURE;
   }

   if(mapserv->QueryFile) {
      status = msLoadQuery(mapserv->map, mapserv->QueryFile);
      if(status != MS_SUCCESS) return MS_FAILURE;
   }

   status = setExtent(mapserv);
   if(status != MS_SUCCESS) return MS_FAILURE;
   status = checkWebScale(mapserv);
   if(status != MS_SUCCESS) return MS_FAILURE;

   /* -------------------------------------------------------------------- */
   /*      generate map, legend, scalebar and refernce images.             */
   /* -------------------------------------------------------------------- */
   if(msGenerateImages(mapserv, MS_FALSE, MS_TRUE) != MS_SUCCESS)
      return MS_FAILURE;

   if ( (template != NULL) && (strcasecmp(template, "openlayers")==0) ) {
      msIO_setHeader("Content-type","text/html");
      msIO_sendHeaders();
      if (msReturnOpenLayersPage(mapserv) != MS_SUCCESS)
         return MS_FAILURE;
   }
   else if(mapserv->QueryFile) {
      if(msReturnTemplateQuery(mapserv, mapserv->map->web.queryformat, NULL) != MS_SUCCESS)
         return MS_FAILURE;
   } else {
      if(TEMPLATE_TYPE(mapserv->map->web.template) == MS_FILE) { /* if thers's an html template, then use it */
         if(mapserv->sendheaders) {
            msIO_setHeader("Content-type",mapserv->map->web.browseformat); /* write MIME header */
            msIO_sendHeaders();
         }
         if(msReturnPage(mapserv, mapserv->map->web.template, BROWSE, NULL) != MS_SUCCESS)
            return MS_FAILURE;
      } else {    
         if(msReturnURL(mapserv, mapserv->map->web.template, BROWSE) != MS_SUCCESS)
            return MS_FAILURE;
      }
   }
   return MS_SUCCESS;
}


int msCGIDispatchCoordinateRequest(mapservObj *mapserv) {
   setCoordinate(mapserv); /* mouse click => map coord */
   msIO_printf("Your \"<i>click</i>\" corresponds to (approximately): (%g, %g).",
             mapserv->mappnt.x, mapserv->mappnt.y);

#ifdef USE_PROJ
  if(mapserv->map->projection.proj != NULL && !pj_is_latlong(mapserv->map->projection.proj) ) {
    pointObj p=mapserv->mappnt;
    msProjectPoint(&(mapserv->map->projection), &(mapserv->map->latlon), &p);
    msIO_printf("Computed lat/lon value is (%g, %g).\n",p.x, p.y);
  }
#endif
  return MS_SUCCESS;
}

int msCGIDispatchQueryRequest(mapservObj *mapserv) {
   int status,i,j;
   char buffer[1024];
   if(mapserv->QueryFile) { /* already got a completed query */
      status = msLoadQuery(mapserv->map, mapserv->QueryFile);
      if(status != MS_SUCCESS) return MS_FAILURE;
   } else {

      if((mapserv->QueryLayerIndex = msGetLayerIndex(mapserv->map, mapserv->QueryLayer)) != -1) /* force the query layer on */
         GET_LAYER(mapserv->map, mapserv->QueryLayerIndex)->status = MS_ON;

      switch(mapserv->Mode) {
         case ITEMFEATUREQUERY:
         case ITEMFEATURENQUERY:
            if((mapserv->SelectLayerIndex = msGetLayerIndex(mapserv->map, mapserv->SelectLayer)) == -1) { /* force the selection layer on */
               msSetError(MS_WEBERR, "Selection layer not set or references an invalid layer.", "mapserv()"); 
               return MS_FAILURE;
            }
            GET_LAYER(mapserv->map, mapserv->SelectLayerIndex)->status = MS_ON;

            /* validate the qstring parameter */
            if(msValidateParameter(mapserv->QueryString, msLookupHashTable(&(GET_LAYER(mapserv->map, mapserv->SelectLayerIndex)->validation), "qstring"), 
                     msLookupHashTable(&(mapserv->map->web.validation), "qstring"), 
                     msLookupHashTable(&(GET_LAYER(mapserv->map, mapserv->SelectLayerIndex)->metadata), "qstring_validation_pattern"), NULL) != MS_SUCCESS) {
               msSetError(MS_WEBERR, "Parameter 'qstring' value fails to validate.", "mapserv()");
               return MS_FAILURE;
            }

            if(mapserv->QueryCoordSource != NONE && !mapserv->UseShapes)
               if(MS_SUCCESS != setExtent(mapserv)) /* set user area of interest */
                  return MS_FAILURE;

            mapserv->map->query.type = MS_QUERY_BY_ATTRIBUTE;
            if(mapserv->QueryItem) mapserv->map->query.item = msStrdup(mapserv->QueryItem);
            if(mapserv->QueryString) mapserv->map->query.str = msStrdup(mapserv->QueryString);

            mapserv->map->query.rect = mapserv->map->extent;

            mapserv->map->query.mode = MS_QUERY_MULTIPLE;
            if(mapserv->Mode == ITEMFEATUREQUERY)
               mapserv->map->query.mode = MS_QUERY_SINGLE;

            mapserv->map->query.layer = mapserv->QueryLayerIndex;
            mapserv->map->query.slayer = mapserv->SelectLayerIndex; /* this will trigger the feature query eventually */
            break;
         case FEATUREQUERY:
         case FEATURENQUERY:
            if((mapserv->SelectLayerIndex = msGetLayerIndex(mapserv->map, mapserv->SelectLayer)) == -1) { /* force the selection layer on */
               msSetError(MS_WEBERR, "Selection layer not set or references an invalid layer.", "mapserv()"); 
               return MS_FAILURE;
            }
            GET_LAYER(mapserv->map, mapserv->SelectLayerIndex)->status = MS_ON;

            if(mapserv->Mode == FEATUREQUERY) {
               switch(mapserv->QueryCoordSource) {
                  case FROMIMGPNT:
                     mapserv->map->extent = mapserv->ImgExt; /* use the existing map extent */    
                     setCoordinate(mapserv);
                     break;
                  case FROMUSERPNT:
                     break;
                  default:
                     msSetError(MS_WEBERR, "No way to perform the initial search, not enough information.", "mapserv()");
                     return MS_FAILURE;
                     break;
               }      

               mapserv->map->query.type = MS_QUERY_BY_POINT;
               mapserv->map->query.mode = MS_QUERY_SINGLE;

               mapserv->map->query.point = mapserv->mappnt;
               mapserv->map->query.buffer = mapserv->Buffer;

               mapserv->map->query.layer = mapserv->QueryLayerIndex;
               mapserv->map->query.slayer = mapserv->SelectLayerIndex; /* this will trigger the feature query eventually */
            } else { /* FEATURENQUERY */
               switch(mapserv->QueryCoordSource) {
                  case FROMIMGPNT:
                     mapserv->map->extent = mapserv->ImgExt; /* use the existing map extent */    
                     setCoordinate(mapserv);
                     mapserv->map->query.type = MS_QUERY_BY_POINT;
                     break;     
                  case FROMIMGBOX:
                     /* TODO: this option was present but with no code to leverage the image box... */
                     break;
                  case FROMUSERPNT:
                     mapserv->map->query.type = MS_QUERY_BY_POINT;
                  default:
                     if(MS_SUCCESS != setExtent(mapserv)) {
                        return MS_FAILURE;
                     }
                     mapserv->map->query.type = MS_QUERY_BY_RECT;
                     break;
               }
            }

            mapserv->map->query.mode = MS_QUERY_MULTIPLE;

            mapserv->map->query.rect = mapserv->map->extent;
            mapserv->map->query.point = mapserv->mappnt;
            mapserv->map->query.buffer = mapserv->Buffer;

            mapserv->map->query.layer = mapserv->QueryLayerIndex;
            mapserv->map->query.slayer = mapserv->SelectLayerIndex;
            break;
         case ITEMQUERY:
         case ITEMNQUERY:
            if(mapserv->QueryLayerIndex < 0 || mapserv->QueryLayerIndex >= mapserv->map->numlayers) {
               msSetError(MS_WEBERR, "Query layer not set or references an invalid layer.", "mapserv()"); 
               return MS_FAILURE;
            }

            /* validate the qstring parameter */
            if(msValidateParameter(mapserv->QueryString, msLookupHashTable(&(GET_LAYER(mapserv->map, mapserv->QueryLayerIndex)->validation), "qstring"),
                     msLookupHashTable(&(mapserv->map->web.validation), "qstring"),
                     msLookupHashTable(&(GET_LAYER(mapserv->map, mapserv->QueryLayerIndex)->metadata), "qstring_validation_pattern"), NULL) != MS_SUCCESS) {
               msSetError(MS_WEBERR, "Parameter 'qstring' value fails to validate.", "mapserv()");
               return MS_FAILURE;
            }

            if(mapserv->QueryCoordSource != NONE && !mapserv->UseShapes)
               if(MS_SUCCESS != setExtent(mapserv)) /* set user area of interest */
                     return MS_FAILURE;

            mapserv->map->query.type = MS_QUERY_BY_ATTRIBUTE;
            mapserv->map->query.layer = mapserv->QueryLayerIndex;
            if(mapserv->QueryItem) mapserv->map->query.item = msStrdup(mapserv->QueryItem);
            if(mapserv->QueryString) mapserv->map->query.str = msStrdup(mapserv->QueryString);

            mapserv->map->query.rect = mapserv->map->extent;

            mapserv->map->query.mode = MS_QUERY_MULTIPLE;
            if(mapserv->Mode == ITEMQUERY) mapserv->map->query.mode = MS_QUERY_SINGLE;
            break;
         case NQUERY:
            mapserv->map->query.mode = MS_QUERY_MULTIPLE; /* all of these cases return multiple results */
            mapserv->map->query.layer = mapserv->QueryLayerIndex;

            switch(mapserv->QueryCoordSource) {
               case FROMIMGPNT:      
                  setCoordinate(mapserv);

                  if(mapserv->SearchMap) { /* compute new extent, pan etc then search that extent */
                     if(MS_SUCCESS != setExtent(mapserv)) /* set user area of interest */
                        return MS_FAILURE;
                     mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height);
                     if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return MS_FAILURE;
                     mapserv->map->query.rect = mapserv->map->extent;
                     mapserv->map->query.type = MS_QUERY_BY_RECT; 
                  } else {
                     mapserv->map->extent = mapserv->ImgExt; /* use the existing image parameters */
                     mapserv->map->width = mapserv->ImgCols;
                     mapserv->map->height = mapserv->ImgRows;
                     if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return MS_FAILURE;
                     mapserv->map->query.point = mapserv->mappnt;
                     mapserv->map->query.type = MS_QUERY_BY_POINT;
                  }

                  break;      
               case FROMIMGBOX:      
                  if(mapserv->SearchMap) { /* compute new extent, pan etc then search that extent */
                     setExtent(mapserv);
                     if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return MS_FAILURE;
                     mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height);
                     mapserv->map->query.rect = mapserv->map->extent;
                     mapserv->map->query.type = MS_QUERY_BY_RECT;
                  } else {
                     double cellx, celly;

                     mapserv->map->extent = mapserv->ImgExt; /* use the existing image parameters */
                     mapserv->map->width = mapserv->ImgCols;
                     mapserv->map->height = mapserv->ImgRows;
                     if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return MS_FAILURE;    
                     cellx = MS_CELLSIZE(mapserv->ImgExt.minx, mapserv->ImgExt.maxx, mapserv->ImgCols); /* calculate the new search extent */
                     celly = MS_CELLSIZE(mapserv->ImgExt.miny, mapserv->ImgExt.maxy, mapserv->ImgRows);
                     mapserv->RawExt.minx = MS_IMAGE2MAP_X(mapserv->ImgBox.minx, mapserv->ImgExt.minx, cellx);          
                     mapserv->RawExt.maxx = MS_IMAGE2MAP_X(mapserv->ImgBox.maxx, mapserv->ImgExt.minx, cellx);
                     mapserv->RawExt.maxy = MS_IMAGE2MAP_Y(mapserv->ImgBox.miny, mapserv->ImgExt.maxy, celly); /* y's are flip flopped because img/map coordinate systems are */
                     mapserv->RawExt.miny = MS_IMAGE2MAP_Y(mapserv->ImgBox.maxy, mapserv->ImgExt.maxy, celly);

                     mapserv->map->query.rect = mapserv->RawExt;
                     mapserv->map->query.type = MS_QUERY_BY_RECT;
                  }
                  break;
               case FROMIMGSHAPE:
                  mapserv->map->extent = mapserv->ImgExt; /* use the existing image parameters */
                  mapserv->map->width = mapserv->ImgCols;
                  mapserv->map->height = mapserv->ImgRows;
                  mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height);
                  if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return MS_FAILURE;

                  /* convert from image to map coordinates here (see setCoordinate) */
                  for(i=0; i<mapserv->map->query.shape->numlines; i++) {
                     for(j=0; j<mapserv->map->query.shape->line[i].numpoints; j++) {
                        mapserv->map->query.shape->line[i].point[j].x = MS_IMAGE2MAP_X(mapserv->map->query.shape->line[i].point[j].x, mapserv->map->extent.minx, mapserv->map->cellsize);
                        mapserv->map->query.shape->line[i].point[j].y = MS_IMAGE2MAP_Y(mapserv->map->query.shape->line[i].point[j].y, mapserv->map->extent.maxy, mapserv->map->cellsize);             
                     }
                  }

                  mapserv->map->query.type = MS_QUERY_BY_SHAPE;
                  break;      
               case FROMUSERPNT:
                  if(mapserv->Buffer == 0) { /* do a *pure* point query */
                     mapserv->map->query.point = mapserv->mappnt;
                     mapserv->map->query.type = MS_QUERY_BY_POINT;
                     setExtent(mapserv);
                  } else {
                     setExtent(mapserv);
                     if(mapserv->SearchMap) { /* the extent should be tied to a map, so we need to "adjust" it */
                        if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return MS_FAILURE;
                        mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height); 
                     }
                     mapserv->map->query.rect = mapserv->map->extent;
                     mapserv->map->query.type = MS_QUERY_BY_RECT;
                  }
                  break;
               case FROMUSERSHAPE:
                  setExtent(mapserv);
                  mapserv->map->query.type = MS_QUERY_BY_SHAPE;
                  break;
               default: /* from an extent of some sort */
                  setExtent(mapserv);
                  if(mapserv->SearchMap) { /* the extent should be tied to a map, so we need to "adjust" it */
                     if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return MS_FAILURE;
                     mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height);
                  }

                  mapserv->map->query.rect = mapserv->map->extent;
                  mapserv->map->query.type = MS_QUERY_BY_RECT;
                  break;
            }
            break;
         case QUERY:
            switch(mapserv->QueryCoordSource) {
               case FROMIMGPNT:
                  setCoordinate(mapserv);
                  mapserv->map->extent = mapserv->ImgExt; /* use the existing image parameters */
                  mapserv->map->width = mapserv->ImgCols;
                  mapserv->map->height = mapserv->ImgRows;
                  if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return MS_FAILURE;
                  break;
               case FROMUSERPNT: /* only a buffer makes sense, DOES IT? */    
                  if(setExtent(mapserv) != MS_SUCCESS) return MS_FAILURE;
                  break;
               default:
                  msSetError(MS_WEBERR, "Query mode needs a point, imgxy and mapxy are not set.", "mapserv()");
                  return MS_FAILURE;
                  break;
            }

            mapserv->map->query.type = MS_QUERY_BY_POINT;
            mapserv->map->query.mode = MS_QUERY_SINGLE;
            mapserv->map->query.layer = mapserv->QueryLayerIndex;
            mapserv->map->query.point = mapserv->mappnt;
            mapserv->map->query.buffer = mapserv->Buffer;          
            break;
         case INDEXQUERY:
            mapserv->map->query.type = MS_QUERY_BY_INDEX;
            mapserv->map->query.mode = MS_QUERY_SINGLE;
            mapserv->map->query.layer = mapserv->QueryLayerIndex;
            mapserv->map->query.shapeindex = mapserv->ShapeIndex;
            mapserv->map->query.tileindex = mapserv->TileIndex;
            break;
      } /* end mode switch */

      /* finally execute the query */
      if((status = msExecuteQuery(mapserv->map)) != MS_SUCCESS) return MS_FAILURE;
   }

   if(mapserv->map->querymap.width != -1) mapserv->map->width = mapserv->map->querymap.width; /* make sure we use the right size */
   if(mapserv->map->querymap.height != -1) mapserv->map->height = mapserv->map->querymap.height;

   if(mapserv->UseShapes)
      if(MS_SUCCESS != setExtentFromShapes(mapserv))
         return MS_FAILURE;

   if(msReturnTemplateQuery(mapserv, mapserv->map->web.queryformat, NULL) != MS_SUCCESS) return MS_FAILURE;

   if(mapserv->savequery) {
      snprintf(buffer, sizeof(buffer), "%s%s%s%s", mapserv->map->web.imagepath, mapserv->map->name, mapserv->Id, MS_QUERY_EXTENSION);
      if((status = msSaveQuery(mapserv->map, buffer, MS_FALSE)) != MS_SUCCESS) return status;
   }
   return MS_SUCCESS;
}

int msCGIDispatchImageRequest(mapservObj *mapserv) {
   int status;
   imageObj *img;
   switch(mapserv->Mode) {
      case MAP:
         if(mapserv->QueryFile) {
            status = msLoadQuery(mapserv->map, mapserv->QueryFile);
            if(status != MS_SUCCESS) return MS_FAILURE;
            img = msDrawMap(mapserv->map, MS_TRUE);
         } else
            img = msDrawMap(mapserv->map, MS_FALSE);
         break;
      case REFERENCE:
         mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height);
         img = msDrawReferenceMap(mapserv->map);
         break;      
      case SCALEBAR:
         img = msDrawScalebar(mapserv->map);
         break;
      case TILE:
         msTileSetExtent(mapserv);
         img = msTileDraw(mapserv);
         break;
      case LEGEND:
         img = msDrawLegend(mapserv->map, MS_FALSE);
         break;
   }

   if(!img) return MS_FAILURE;

   /*
    ** Set the Cache control headers if the option is set. 
    */
   if( mapserv->sendheaders && msLookupHashTable(&(mapserv->map->web.metadata), "http_max_age") ) {
      msIO_setHeader("Cache-Control","max-age=%s", msLookupHashTable(&(mapserv->map->web.metadata), "http_max_age"));
   }

   if(mapserv->sendheaders)  {
      const char *attachment = msGetOutputFormatOption(mapserv->map->outputformat, "ATTACHMENT", NULL ); 
      if(attachment)
         msIO_setHeader("Content-disposition","attachment; filename=%s", attachment);
      msIO_setHeader("Content-type",MS_IMAGE_MIME_TYPE(mapserv->map->outputformat));
      msIO_sendHeaders();
   }

   if( mapserv->Mode == MAP || mapserv->Mode == TILE )
      status = msSaveImage(mapserv->map, img, NULL);
   else
      status = msSaveImage(NULL,img, NULL);

   if(status != MS_SUCCESS) return MS_FAILURE;

   msFreeImage(img);
   return MS_SUCCESS;
}


int msCGIDispatchLegendRequest(mapservObj *mapserv) {
   if(mapserv->map->legend.template) {
      char *legendTemplate;
      legendTemplate = generateLegendTemplate(mapserv);
      if(legendTemplate) {
         if(mapserv->sendheaders) {
            msIO_setHeader("Content-type",mapserv->map->web.legendformat);
            msIO_sendHeaders();
         }
         msIO_fwrite(legendTemplate, strlen(legendTemplate), 1, stdout);

         free(legendTemplate);
         return MS_SUCCESS;
      } else {/* error already generated by (generateLegendTemplate()) */
         return MS_FAILURE;
      }
   } else {
      return msCGIDispatchImageRequest(mapserv);
   }
}

int msCGIDispatchLegendIconRequest(mapservObj *mapserv) {
   char **tokens;
   int numtokens=0;
   int layerindex=-1, classindex=0;
   outputFormatObj *format = NULL;
   imageObj *img;

   /* TODO: do we want to set scale here? */

   /* do we have enough information */
   if(!mapserv->icon) {
      msSetError(MS_WEBERR, "Mode=LEGENDICON requires an icon parameter.", "mapserv()");
      return MS_FAILURE;
   }

   /* process the icon definition */
   tokens = msStringSplit(mapserv->icon, ',', &numtokens);

   if(numtokens != 1 && numtokens != 2) {
      msSetError(MS_WEBERR, "%d Malformed icon parameter, should be 'layer,class' or just 'layer' if the layer has only 1 class defined.", "mapserv()", numtokens);
      return MS_FAILURE;
   }

   if((layerindex = msGetLayerIndex(mapserv->map, tokens[0])) == -1) {
      msSetError(MS_WEBERR, "Icon layer=%s not found in mapfile.", "mapserv()", tokens[0]);
      return MS_FAILURE;
   }

   if(numtokens == 2) { /* check the class index */
      classindex = atoi(tokens[1]);
      if(classindex >= GET_LAYER(mapserv->map, layerindex)->numclasses) {
         msSetError(MS_WEBERR, "Icon class=%d not found in layer=%s.", "mapserv()", classindex, GET_LAYER(mapserv->map, layerindex)->name);
         return MS_FAILURE;
      }
   }

   /* ensure we have an image format representing the options for the legend. */
   msApplyOutputFormat(&format, mapserv->map->outputformat, mapserv->map->legend.transparent, mapserv->map->legend.interlace, MS_NOOVERRIDE);

   /* initialize the legend image */
   if( ! MS_RENDERER_PLUGIN(format) ) {
      msSetError(MS_RENDERERERR, "unsupported renderer for legend icon", "mapserv main()");
      return MS_FAILURE;
   }
   img = msImageCreate(mapserv->map->legend.keysizex, mapserv->map->legend.keysizey, format,
         mapserv->map->web.imagepath, mapserv->map->web.imageurl, mapserv->map->resolution, mapserv->map->defresolution,
         &(mapserv->map->legend.imagecolor));

   /* drop this reference to output format */
   msApplyOutputFormat(&format, NULL, MS_NOOVERRIDE, MS_NOOVERRIDE, MS_NOOVERRIDE);

   if(msDrawLegendIcon(mapserv->map, GET_LAYER(mapserv->map, layerindex), GET_LAYER(mapserv->map, layerindex)->class[classindex], mapserv->map->legend.keysizex,  mapserv->map->legend.keysizey, img, 0, 0) != MS_SUCCESS)
      return MS_FAILURE;

   if(mapserv->sendheaders) {
      msIO_setHeader("Content-type",MS_IMAGE_MIME_TYPE(mapserv->map->outputformat));
      msIO_sendHeaders();
   }
   /*
    ** Set the Cache control headers if the option is set. 
    */
   if( mapserv->sendheaders && msLookupHashTable(&(mapserv->map->web.metadata), "http_max_age") ) {
      msIO_printf("Cache-Control: max-age=%s%c", msLookupHashTable(&(mapserv->map->web.metadata), "http_max_age"), 10);
   }
   if( msSaveImage(NULL, img, NULL) != MS_SUCCESS)
      return MS_FAILURE;

   msFreeCharArray(tokens, numtokens);
   msFreeImage(img);
   return MS_SUCCESS;
}

int msCGIDispatchRequest(mapservObj *mapserv) {
   int i;
   int status;
   
   /*
    ** Determine 'mode': Check for MS_MODE env. var. and mode=... CGI param
    */
   mapserv->Mode = -1; /* Not set */
   if( msCGISetMode(mapserv) != MS_SUCCESS) {
      return MS_FAILURE;
   }
   
   /*
    ** Start by calling the WMS/WFS/WCS Dispatchers.  If they fail then we'll 
    ** process this as a regular MapServer request.
    */
   if((mapserv->Mode == -1 || mapserv->Mode == OWS || mapserv->Mode == WFS) &&
         (status = msOWSDispatch(mapserv->map, mapserv->request, 
                                 mapserv->Mode)) != MS_DONE  )  {
      /*
       ** OWSDispatch returned either MS_SUCCESS or MS_FAILURE
       */
      if( status == MS_FAILURE ) {
         return MS_FAILURE;
      }

      if (status == MS_SUCCESS &&
            strcasecmp(mapserv->map->imagetype, "application/openlayers")==0)
      {
         char *service = NULL;
         for( i=0; i<mapserv->request->NumParams; i++)
         {
            if(strcasecmp(mapserv->request->ParamNames[i], "SERVICE") == 0) {
               service = mapserv->request->ParamValues[i];
               break;
            }
         }
         if (service && strcasecmp(service,"WMS")==0)
         {
            if(mapserv->sendheaders) {
               msIO_setHeader("Content-type","text/html");
               msIO_sendHeaders();
            }

            if (msReturnOpenLayersPage(mapserv) != MS_SUCCESS)
               return MS_FAILURE;
         }
      }
      return MS_SUCCESS;
   } /* done OGC/OWS case */


    /*
    ** Do "traditional" mode processing.
    */
    if (mapserv->Mode == -1)
        mapserv->Mode = BROWSE;

    if(MS_SUCCESS != msCGILoadForm(mapserv)) {
       return MS_FAILURE;
    }
 
/* Insecure as implemented, need to save someplace non accessible by everyone in the universe
    if(mapserv->savemap) {
      snprintf(buffer, sizeof(buffer), "%s%s%s.map", mapserv->map->web.imagepath, mapserv->map->name, mapserv->Id);
      if(msSaveMap(mapserv->map, buffer) == -1) return MS_FAILURE;
    }
*/

    if((mapserv->CoordSource == FROMIMGPNT) || (mapserv->CoordSource == FROMIMGBOX)) /* make sure extent of existing image matches shape of image */
      mapserv->map->cellsize = msAdjustExtent(&mapserv->ImgExt, mapserv->ImgCols, mapserv->ImgRows);

    /*
    ** For each layer let's set layer status
    */
    for(i=0;i<mapserv->map->numlayers;i++) {
      if((GET_LAYER(mapserv->map, i)->status != MS_DEFAULT)) {
        if(isOn(mapserv,  GET_LAYER(mapserv->map, i)->name, GET_LAYER(mapserv->map, i)->group) == MS_TRUE) /* Set layer status */
          GET_LAYER(mapserv->map, i)->status = MS_ON;
        else
          GET_LAYER(mapserv->map, i)->status = MS_OFF;
      }
    }

    if(mapserv->CoordSource == FROMREFPNT) /* force browse mode if the reference coords are set */
      mapserv->Mode = BROWSE;

    if(mapserv->Mode == TILE) {
       /*
        ** Tile mode:
        ** Set the projection up and test the parameters for legality.
        */
       if( msTileSetup(mapserv) != MS_SUCCESS ) {
          return MS_FAILURE;
       }
    }
    if(mapserv->Mode == BROWSE) {
       return msCGIDispatchBrowseRequest(mapserv);
    } else if(mapserv->Mode == MAP || mapserv->Mode == SCALEBAR || mapserv->Mode == REFERENCE || mapserv->Mode == TILE) { /* "image" only modes */
       /* tile, map, scalebar and reference all need the extent to be set up correctly */
       if(setExtent(mapserv) != MS_SUCCESS) return MS_FAILURE;
       if(checkWebScale(mapserv) != MS_SUCCESS) return MS_FAILURE;
       return msCGIDispatchImageRequest(mapserv);
    } else if(mapserv->Mode == LEGEND) {
       return msCGIDispatchLegendRequest(mapserv);
    } else if(mapserv->Mode == LEGENDICON) {
       return msCGIDispatchLegendIconRequest(mapserv);
    } else if(mapserv->Mode >= QUERY) {
       return msCGIDispatchQueryRequest(mapserv);
    } else if(mapserv->Mode == COORDINATE) {
       return msCGIDispatchCoordinateRequest(mapserv);
    }
    else {
       msSetError(MS_WEBERR, "Bug: unsupported mode", "msDispatchRequest");
       return MS_FAILURE;
    }
}

int msCGIHandler(const char *query_string, void **out_buffer, size_t *buffer_length) {
   int x,m=0;
   struct mstimeval execstarttime, execendtime;
   struct mstimeval requeststarttime, requestendtime;
   mapservObj* mapserv = NULL;
   char *queryString;
   int maxParams = MS_DEFAULT_CGI_PARAMS;
   msIOContext *ctx;
   msIOBuffer  *buf;

   msIO_installStdoutToBuffer();
   /* Use MS_ERRORFILE and MS_DEBUGLEVEL env vars if set */
   if( msDebugInitFromEnv() != MS_SUCCESS ) {
      msCGIWriteError(mapserv);
      goto end_request;
   }

   if(msGetGlobalDebugLevel() >= MS_DEBUGLEVEL_TUNING) 
      msGettimeofday(&execstarttime, NULL);

   mapserv = msAllocMapServObj();
   mapserv->request->type = MS_GET_REQUEST;

   if(!query_string || !*query_string) {
      msIO_setHeader("Content-type","text/html");
      msIO_sendHeaders();
      msIO_printf("No query information to decode. QUERY_STRING not set.\n");	
      goto end_request;
   }

   /* don't modify the string */
   queryString = msStrdup(query_string);
   for(x=0;queryString[0] != '\0';x++) {
      if(m >= maxParams) {
         maxParams *= 2;
         mapserv->request->ParamNames = (char **) realloc(mapserv->request->ParamNames,sizeof(char *) * maxParams);
         if (mapserv->request->ParamNames == NULL) {
            msIO_printf("Out of memory trying to allocate name/value pairs.\n");
            goto end_request;
         }
         mapserv->request->ParamValues = (char **) realloc(mapserv->request->ParamValues,sizeof(char *) * maxParams);
         if (mapserv->request->ParamValues ==  NULL) {
            msIO_printf("Out of memory trying to allocate name/value pairs.\n");
            goto end_request;
         }
      } 
      mapserv->request->ParamValues[m] = makeword(queryString,'&');
      plustospace(mapserv->request->ParamValues[m]);
      unescape_url(mapserv->request->ParamValues[m]);
      mapserv->request->ParamNames[m] = makeword(mapserv->request->ParamValues[m],'=');
      m++;
   }
   mapserv->request->NumParams = m;

   if( mapserv->request->NumParams == 0 ) {
      msCGIWriteError(mapserv);
      goto end_request;
   }

   mapserv->map = msCGILoadMap(mapserv);
   if(!mapserv->map) {
      msCGIWriteError(mapserv);
      goto end_request;
   }

   if( mapserv->map->debug >= MS_DEBUGLEVEL_TUNING) 
      msGettimeofday(&requeststarttime, NULL);


   if(msCGIDispatchRequest(mapserv) != MS_SUCCESS) {
      msCGIWriteError(mapserv);
      goto end_request;
   }


end_request:
   if(mapserv) {
      if(mapserv->map && mapserv->map->debug >= MS_DEBUGLEVEL_TUNING) {
         msGettimeofday(&requestendtime, NULL);
         msDebug("mapserv request processing time (msLoadMap not incl.): %.3fs\n", 
               (requestendtime.tv_sec+requestendtime.tv_usec/1.0e6)-
               (requeststarttime.tv_sec+requeststarttime.tv_usec/1.0e6) );
      }
      msCGIWriteLog(mapserv,MS_FALSE);
      msFreeMapServObj(mapserv);
   }

   /* normal case, processing is complete */
   if(msGetGlobalDebugLevel() >= MS_DEBUGLEVEL_TUNING) {
      msGettimeofday(&execendtime, NULL);
      msDebug("mapserv total execution time: %.3fs\n", 
            (execendtime.tv_sec+execendtime.tv_usec/1.0e6)-
            (execstarttime.tv_sec+execstarttime.tv_usec/1.0e6) );
   }
   ctx = msIO_getHandler( (FILE *) "stdout" );
   buf = (msIOBuffer *) ctx->cbData;
   *out_buffer = buf->data;
   *buffer_length = buf->data_offset;

   free(queryString);

   return 0;
}