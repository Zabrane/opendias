function getScanProgress (progressId, device) {

  $.ajax({ url: "dynamic",
	 dataType: "xml",
	 data: {action: "getScanningProgress", 
		scanprogressid: progressId,
	       },
	 cache: false,
	 type: "POST",
	 success: function(dta) {
           var finish = 0;
           status = parseInt( $(dta).find('status').text() );
           vvalue = parseInt( $(dta).find('value').text() );

           if( status == 0 ) { // SCAN_IDLE,
             $('#status_'+device).text("Setting up.");
             // Give us a chance ....

           } else if( status == 1 ) { // SCAN_INTERNAL_ERROR,
             $('#status_'+device).text("Internal Error.");
             alert("Internal Error: " + vvalue);
             finish = 1;

           } else if( status == 2 ) { // SCAN_WAITING_ON_SCANNER,
             $('#status_'+device).text("Setting up.");
             // Give us a chance ....

           } else if( status == 3 ) { // SCAN_ERRO_FROM_SCANNER,// SANE error code
	     $("#progressbar_"+device).progressbar( "destroy" );
             $('#status_'+device).text("Error while scanning.");
             alert("Scanner Error: " + vvalue);
             finish = 1;

           } else if( status == 4 ) { // SCAN_SCANNING,// Current progress
             $('#status_'+device).text("Scanning in progress.");
	     $("#progressbar_"+device).progressbar({
	       value: vvalue,
	     });

           } else if( status == 5 ) { // SCAN_WAITING_ON_NEW_PAGE,// Waiting for page [x]
	     $("#progressbar_"+device).progressbar( "destroy" );
             $('#status_'+device).text("Please insert page "+vvalue+".");
             alert("Please insert page "+vvalue+".");

           } else if( status == 6 ) { // SCAN_TIMEOUT_WAITING_ON_NEW_PAGE,
             $('#status_'+device).text("Timeout while waiting for the next page.");
             alert("Timeout waiting on the new page insert.");
             finish = 1;

           } else if( status == 7 ) { // SCAN_CONVERTING_FORMAT,
	     $("#progressbar_"+device).progressbar( "destroy" );
             $('#status_'+device).text("Converting scanned image format.");

           } else if( status == 8 ) { // SCAN_ERROR_CONVERTING_FORMAT,// FreeImage error code
	     $("#progressbar_"+device).progressbar( "destroy" );
             $('#status_'+device).text("Error while converting scanned image format.");
             alert("Image Processing Error: " + vvalue);
             finish = 1;

           } else if( status == 9 ) { // SCAN_PERFORMING_OCR,
	     $("#progressbar_"+device).progressbar( "destroy" );
             $('#status_'+device).text("Performing OCR on scanned image.");

           } else if( status == 10 ) { // SCAN_ERROR_PERFORMING_OCR,// xxxxxx error code
	     $("#progressbar_"+device).progressbar( "destroy" );
             $('#status_'+device).text("Error while performing OCR operation.");
             alert("OCR Error: " + vvalue);
             finish = 1;

           } else if( status == 11 ) { // SCAN_RESERVED_1,
           } else if( status == 12 ) { // SCAN_RESERVED_2,
           } else if( status == 13 ) { // SCAN_RESERVED_3,
           } else if( status == 14 ) { // SCAN_RESERVED_4,
           } else if( status == 15 ) { // SCAN_RESERVED_5,
           } else if( status == 16 ) { // SCAN_FINISHED
	     $("#progressbar_"+device).progressbar( "destroy" );
             $('#status_'+device).text("Scan operation complete.");
             document.location.href = "/docDetail.html?docid="+vvalue;
             finish = 1;

           }

           if(finish == 0) {
             setTimeout("getScanProgress('"+progressId+"','"+device+"')", 1000);
           }

	 }
       });
}


$(document).ready(function() {

  $("#tabs").tabs();

  $('#loading').canvasLoader({'radius':20, 'dotRadius':2});

  $.ajax({ url: "dynamic",
         dataType: "xml",
         data: {action: "getScannerList"},
         cache: false,
         type: "POST",
         success: function(data){
           if( $(data).find('error').text() ){
             alert("Error in fetching details.");
             return 1;
           }
           var deviceid=0;
           $(data).find('device').each( function() {

             deviceid++;
             var device=deviceid;

             // Create scan doc
             var div = document.createElement("div");
             div.setAttribute('id','deviceTab_'+device);
             var newTabHtml = document.getElementById('scannerTemplate').innerHTML;
             idchange = new Array('title', 'deviceid', 'format', 'pages', 'pagesSlider', 'resolution', 
                        'resolutionSlider', 'ocr', 'progressbar', 'resolutionDisplay', 'pagesDisplay', 
                        'scanButton', 'status', 'hlp', 'q', 'resolutionGood');
             for (change in idchange) {
               //alert("replace: '" + idchange[change]+"_DEVICE'   with    '" + idchange[change]+"_"+device + "'.");
               newTabHtml = newTabHtml.replace(new RegExp(idchange[change]+"_DEVICE","g"), idchange[change]+"_"+device);
             }
             div.innerHTML = newTabHtml;
             $('#tabs').append(div);

             // Create new tab
             $('#tabs').tabs("add",'#deviceTab_'+device, 
                                      $(this).find("type").text() + ": " + 
                                      $(this).find("vendor").text() + " - " + 
                                      $(this).find("model").text() );

             // Bring the tab contents up-2-date
             $('#title_'+device).text( $(this).find("type").text() + ": " +
                                      $(this).find("vendor").text() + " - " +
                                      $(this).find("model").text() );
             $('#deviceid_'+device).val( $(this).find("name").text() );
             $('#hlp_'+device).val( $(this).find("hlp").text() );
             $('#q_'+device).val( $(this).find("q").text() );
             $('#format_'+device).append('<option>'+$(this).find("format").text()+'</option>');
             $("#resolutionSlider_"+device).slider({
               range: "min",
               value: parseInt($(this).find("default").text()),
               min: parseInt($(this).find("min").text()),
               max: parseInt($(this).find("max").text()),
               step: 50,
               slide: function(event, ui) {
                 $("#resolution_"+device).val( ui.value );
                 $("#resolutionDisplay_"+device).text( ui.value + " dpi" );
               }
             });
             var bestLow = 300;
             var bestHigh = 800;
             var resFactor = 215 / (parseInt($(this).find("max").text()) - parseInt($(this).find("min").text()) );
             $("#resolutionGood_"+device).css( { 'left': resFactor * bestLow,
                                                 'width': (bestHigh - bestLow) * resFactor } );
             $("#resolution_"+device).val( $(this).find("default").text() );
             $("#resolutionDisplay_"+device).text( $(this).find("default").text() + " dpi" );
             $("#pagesSlider_"+device).slider({
               range: "min",
               value: 2,
               min: 1,
               max: 10,
               slide: function(event, ui) {
                 $("#pages_"+device).val( ui.value );
                 $("#pagesDisplay_"+device).text( ui.value + " pages" );
               }
             });
             $("#scanButton_"+device).click( function() {
               $.ajax({ url: "dynamic",
                        dataType: "xml",
                        data: {action: "doScan", 
                               deviceid: $("#deviceid_"+device).val(),
                               format: $("#format_"+device).val(),
                               pages: $("#pages_"+device).val(),
                               resolution: $("#resolution_"+device).val(),
                               ocr: $("#ocr_"+device).val(),
                               hlp: $("#hlp_"+device).val(),
                               q: $("#q_"+device).val(),
                              },
                        cache: false,
                        type: "POST",
                        success: function(data){
                          scanuuid = $(data).find('scanuuid').text();
                          getScanProgress(scanuuid, device);
                        }
                      });
             });
           });
         $('#loading').canvasLoaderHalt();
         $('#scanning').hide();
         }
  });

});

