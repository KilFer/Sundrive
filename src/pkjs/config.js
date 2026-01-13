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
      },
      {
        "type": "slider",
        "messageKey": "step_goal",
        "label": "Daily Step Goal",
        "defaultValue": 8000,
        "min": 0,
        "max": 20000,
        "step": 1000,
        "description": "Set to 0 to disable step tracker"
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
