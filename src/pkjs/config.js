module.exports = [
  {
    "type": "heading",
    "defaultValue": "Sundrive Settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "toggle",
        "messageKey": "date_format_us",
        "label": "Date Format",
        "description": "Use Month/Day format instead of Day/Month",
        "defaultValue": false
      },
      {
        "type": "toggle",
        "messageKey": "show_day_of_week",
        "label": "Show Day of Week",
        "description": "Display day abbreviation (Mo, Tu, We...)",
        "defaultValue": true
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
