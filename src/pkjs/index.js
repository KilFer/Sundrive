// Sundrive JavaScript component - handles API communication
var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var testMode = false; // Set to true to use local test data

// Store last known position
var lastPosition = null;

// Cache key for localStorage
var CACHE_KEY = 'twilight_cache';

var exampleData = {
  "sunrise": "6:11:35 AM",
  "sunset": "6:12:31 PM",
  "solar_noon": "12:12:03 PM",
  "day_length": "12:00:56",
  "civil_twilight_begin": "5:45:21 AM",
  "civil_twilight_end": "6:38:45 PM",
  "nautical_twilight_begin": "5:13:02 AM",
  "nautical_twilight_end": "7:11:04 PM",
  "astronomical_twilight_begin": "4:40:13 AM",
  "astronomical_twilight_end": "7:43:53 PM"
}

// Round coordinate to 2 decimal places
function roundCoordinate(coord) {
  return Math.round(coord * 100) / 100;
}

// Get current date as YYYY-MM-DD string
function getCurrentDateString() {
  var now = new Date();
  var year = now.getFullYear();
  var month = String(now.getMonth() + 1).padStart(2, '0');
  var day = String(now.getDate()).padStart(2, '0');
  return year + '-' + month + '-' + day;
}

// Get moon phase
const getJulianDate = (date = new Date()) => {
  const time = date.getTime();
  const tzoffset = date.getTimezoneOffset()

  return (time / 86400000) - (tzoffset / 1440) + 2440587.5;
}

const LUNAR_MONTH = 29.530588853;

const getLunarAge = (date = new Date()) => {
  const percent = getLunarAgePercent(date);
  const age = percent * LUNAR_MONTH;
  return age;
}
const getLunarAgePercent = (date = new Date()) => {
  return normalize((getJulianDate(date) - 2451550.1) / LUNAR_MONTH);
}
const normalize = value => {
  value = value - Math.floor(value);
  if (value < 0)
    value = value + 1
  return value;
}

const getLunarPhase = (date = new Date()) => {
  const age = getLunarAge(date);
  if (age < 1.84566)
    return "NEW" + age; // NEW
  else if (age < 5.53699)
    return "WXC" + age; // WAXING CRESCENT
  else if (age < 9.22831)
    return "FQ" + age; // FIRST QUARTER
  else if (age < 12.91963)
    return "WXG" + age; // WAXING GIBBOUS
  else if (age < 16.61096)
    return "FUL" + age; // FULL
  else if (age < 20.30228)
    return "WNG" + age; // WANING GIBBOUS
  else if (age < 23.99361)
    return "LQ" + age; // LAST QUARTER
  else if (age < 27.68493)
    return "WNC" + age; // WANING CRESCENT
  return "NEW" + age; // NEW
}

// Save cache to localStorage
function saveCache(latitude, longitude, tzid, data) {
  var cache = {
    date: getCurrentDateString(),
    latitude: roundCoordinate(latitude),
    longitude: roundCoordinate(longitude),
    tzid: tzid,
    data: data
  };

  try {
    localStorage.setItem(CACHE_KEY, JSON.stringify(cache));
    console.log('Cache saved: ' + JSON.stringify(cache));
  } catch (e) {
    console.log('Error saving cache: ' + e);
  }
}

// Load cache from localStorage
function loadCache() {
  try {
    var cacheStr = localStorage.getItem(CACHE_KEY);
    if (cacheStr) {
      var cache = JSON.parse(cacheStr);
      console.log('Cache loaded: ' + JSON.stringify(cache));
      return cache;
    }
  } catch (e) {
    console.log('Error loading cache: ' + e);
  }
  return null;
}

// Validate cache - returns true if cache is still valid
function isCacheValid(cache, latitude, longitude, tzid) {
  if (!cache || !cache.data) {
    console.log('Cache validation: No cache data');
    return false;
  }

  // Check if date has changed (new day)
  var currentDate = getCurrentDateString();
  if (cache.date !== currentDate) {
    console.log('Cache validation: Date changed (' + cache.date + ' -> ' + currentDate + ')');
    return false;
  }

  // Check if timezone has changed
  if (cache.tzid !== tzid) {
    console.log('Cache validation: Timezone changed (' + cache.tzid + ' -> ' + tzid + ')');
    return false;
  }

  // Check if location has moved more than 0.1 degrees (≈11 km)
  var currentLat = roundCoordinate(latitude);
  var currentLng = roundCoordinate(longitude);
  var latDiff = Math.abs(currentLat - cache.latitude);
  var lngDiff = Math.abs(currentLng - cache.longitude);

  if (latDiff >= 0.1 || lngDiff >= 0.1) {
    console.log('Cache validation: Location moved (lat diff: ' + latDiff + ', lng diff: ' + lngDiff + ')');
    return false;
  }

  console.log('Cache validation: Cache is valid');
  return true;
}

// Convert time string from API format to minutes since midnight
function timeStringToMinutes(timeStr) {
  // Format: "7:28:31 AM" or "4:51:53 PM"
  var parts = timeStr.match(/(\d+):(\d+):(\d+)\s+(AM|PM)/);
  if (!parts) return 0;

  var hours = parseInt(parts[1]);
  var minutes = parseInt(parts[2]);
  var isPM = parts[4] === 'PM';

  if (isPM && hours !== 12) hours += 12;
  if (!isPM && hours === 12) hours = 0;

  return hours * 60 + minutes;
}

// Fetch twilight data from API
function fetchTwilightData(latitude, longitude, tzid) {
  console.log('Fetching twilight data for lat:' + latitude + ', lng:' + longitude + ', tzid:' + tzid);

  var url = 'https://api.sunrise-sunset.org/json?lat=' + latitude + '&lng=' + longitude + '&formatted=1&tzid=' + encodeURIComponent(tzid);

  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.onload = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
      try {
        var response = JSON.parse(xhr.responseText);
        if (response.status === 'OK') {
          // Save to cache before sending
          saveCache(latitude, longitude, tzid, response.results);
          sendTwilightData(response.results);
        } else {
          console.log('API returned error status: ' + response.status);
        }
      } catch (e) {
        console.log('Error parsing API response: ' + e);
      }
    } else {
      console.log('API request failed: ' + xhr.status);
    }
  };
  xhr.onerror = function () {
    console.log('Network error fetching twilight data');
  };
  xhr.send();
}

// Send twilight data to watchface
function sendTwilightData(results) {
  console.log('Sending twilight data to watchface');

  var dict = {
    'sunrise': timeStringToMinutes(results.sunrise),
    'sunset': timeStringToMinutes(results.sunset),
    'civil_twilight_begin': timeStringToMinutes(results.civil_twilight_begin),
    'civil_twilight_end': timeStringToMinutes(results.civil_twilight_end),
    'nautical_twilight_begin': timeStringToMinutes(results.nautical_twilight_begin),
    'nautical_twilight_end': timeStringToMinutes(results.nautical_twilight_end),
    'astronomical_twilight_begin': timeStringToMinutes(results.astronomical_twilight_begin),
    'astronomical_twilight_end': timeStringToMinutes(results.astronomical_twilight_end)
  };

  console.log('Data:', JSON.stringify(dict));

  Pebble.sendAppMessage(dict,
    function (e) {
      console.log('Twilight data sent successfully');
    },
    function (e) {
      console.log('Error sending twilight data: ' + e.error.message);
    }
  );
}

// Get current location and fetch data
function updateTwilightData(tzid) {
  if (testMode) {
    console.log('Test mode - using hardcoded data');
    // Using test data from test_json_data.json
    sendTwilightData(exampleData);
    return;
  }

  console.log('getLunarPhase: ' + getLunarPhase());

  // Use provided timezone or default to UTC
  if (!tzid) {
    console.log('No timezone provided, defaulting to UTC');
    tzid = 'UTC';
  }
  console.log('Using timezone: ' + tzid);

  navigator.geolocation.getCurrentPosition(
    function (pos) {
      lastPosition = pos.coords;
      var latitude = pos.coords.latitude;
      var longitude = pos.coords.longitude;

      // Try to load and validate cache
      var cache = loadCache();
      if (isCacheValid(cache, latitude, longitude, tzid)) {
        console.log('Using cached twilight data');
        sendTwilightData(cache.data);
      } else {
        console.log('Cache invalid or expired, fetching from API');
        fetchTwilightData(latitude, longitude, tzid);
      }
    },
    function (err) {
      console.log('Location error: ' + err.message);
      // Fallback to default location (Zaragoza, Spain)
      var defaultLat = 41.65606;
      var defaultLng = -0.87734;

      // Check cache for fallback location
      var cache = loadCache();
      if (isCacheValid(cache, defaultLat, defaultLng, tzid)) {
        console.log('Using cached twilight data for fallback location');
        sendTwilightData(cache.data);
      } else {
        console.log('Fetching from API for fallback location');
        fetchTwilightData(defaultLat, defaultLng, tzid);
      }
    },
    { timeout: 15000, maximumAge: 60000 }
  );
}

// Listen for when the watchface is opened
Pebble.addEventListener('ready', function (e) {
  console.log('PebbleKit JS ready!');

  // Send initial data (not cached)
  console.log('Sending initial example data');
  sendTwilightData(exampleData);

  // Notify watch that JS is ready to receive timezone
  Pebble.sendAppMessage({ 'js_ready': 1 },
    function (e) { console.log('Ready message sent'); },
    function (e) { console.log('Error sending ready message: ' + e.error.message); }
  );
});

// Normalize timezone string (e.g. UTC+1 -> Etc/GMT+1)
function normalizeTimezone(tzid) {
  // Check for UTC+N or UTC-N format
  if (tzid.match(/^UTC[+-]\d+$/)) {
    var normalized = tzid.replace('UTC', 'Etc/GMT');
    console.log('Normalized timezone ' + tzid + ' to ' + normalized);
    return normalized;
  }
  return tzid;
}

// Listen for when an AppMessage is received
Pebble.addEventListener('appmessage', function (e) {
  console.log('AppMessage received from watchface: ' + JSON.stringify(e.payload));

  if (e.payload.timezone_string) {
    var originalTz = e.payload.timezone_string;
    console.log('Received timezone from watch: ' + originalTz);

    var normalizedTz = normalizeTimezone(originalTz);
    updateTwilightData(normalizedTz);
  } else {
    // If no timezone in message (e.g. settings update), assume existing logic or request again?
    // For now, if we get other messages, we might want to check what they are.
    // If it's just a poke to update, we might need a stored timezone or request it again.
    // But updateTwilightData handles missing tzid by defaulting to UTC.
    // Let's assume generic updates might want to re-trigger the flow or use a stored TZ.
    // For simplicity, if we get a generic update request without TZ, we might default to UTC or cached.
    // Ideally the C side sends TZ when it wants an update.
    console.log('Received message without timezone');
  }
});
