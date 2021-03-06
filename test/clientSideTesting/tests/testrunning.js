// On DOM Ready
$q(function() {

  QUnit.config.reorder = false;
  $q.ajaxSetup({async:false});

  // Attempt to load the testswarm inject include (if available)
  $q.getScript("http://testswarmhost/testswarm/js/inject.js");

  var tests = [
              'languageSelectionTest',
              'authenticationTest',
              'listPageTest',
              'acquirePageTest',
              'detailPageTest',
              ];
  var runners = [];

  // Load all the test cases;
  $q.each(tests, function(i,v) {
    console.log('loading: /opendias/includes/tests/'+v+'.js');
    $q.getScript('/opendias/includes/tests/'+v+'.js')
    .done(function(script, textStatus) {
      console.log('loaded: /opendias/includes/tests/'+v+'.js');
      runners.push(v);
    })
    .fail(function(jqxhr, settings, exception){ 
      alert("failed to loaded test '" + v + "', because: " + exception);
    });
  });
  $q.ajaxSetup({async:true});

});

// ====================================

var delay = 100;
var waitOnSetting;

function setupWaitForValue( element, expected ) {
  waitOnSetting = 0;
  if(expected == null) {
    window.waitForValue_original = element.is(':visible');
  } 
  else {
    window.waitForValue_original = element.text();
    console.log( "Saving original value as: " + window.waitForValue_original );
  }
  window.waitForValue_expected = expected;
}

function waitForValue( element, timeout, ignore ) {
  console.log( "waitForValue called with a timeout of "+timeout );

  if ( window.waitForValue_expected == null ) {
    if ( element.is(':visible') != window.waitForValue_original) {
      ok( 1, element.attr('id') + " has changed visibility" );
      waitOnSetting = 1;
      start();
      return;
    }
  }

  else {
    console.log( "Checking : " + element.text() + " / " + window.waitForValue_original );
    current = element.text();
    if ( current != window.waitForValue_original) {
      if( current == ignore) {
        window.waitForValue_original = current;
      }
      else {
        console.log( "actual = " + current + "      started at = " + window.waitForValue_original );
        equal( current, window.waitForValue_expected, element.attr('id') + " was expected to be " + window.waitForValue_expected );
        waitOnSetting = 1;
        start();
        return;
      }
    }
  }

  timeout = timeout - delay;
  if( timeout > 0 ) {
    window.setTimeout( function() { waitForValue(element, timeout, ignore) }, delay);
  }
  else {
    ok( 0, "Timeout while waiting for "+element.attr('id')+" to change.");
    start();
  }

}

function getCookie(name) {
    var nameEQ = name + "=";
    var ca = document.cookie.split(';');
    for(var i=0;i < ca.length;i++) {
        var c = ca[i];
        while (c.charAt(0)==' ') c = c.substring(1,c.length);
        if (c.indexOf(nameEQ) == 0) return c.substring(nameEQ.length,c.length);
    }
    return null;
}

