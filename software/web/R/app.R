# installing packages
# install.packages(c("shiny","tidyverse", "jsonlite")) 

#troubleshooting:
# getwd()
# setwd("/Users/username/Documents/my_project")

#libraries
library(shiny)
library(tidyverse)
library(jsonlite)

# known conflicts in packages:
# (requires package specification for certain methods)
# dplyr::filter() masks stats::filter()
# dplyr::lag()    masks stats::lag()
# jsonlite::validate() masks shiny::validate()

# ------ helper functions ------------

# format_time() is a method that formats a time column with a specific format
format_time <- function(table)
{
    table$time <- as.POSIXct(table$time,
                format = "%Y-%m-%dT%H:%M:%OSZ",
                tz = "UTC")
    table #returning this table
}

# plot_sensor() is a function that returns a scatter plot. This specific method assumes that time is the x axis because it only graphs sensors.
plot_sensor <- function(data, x_row, y_col, y_label) 
{
    ggplot(data, aes(x = .data[[x_row]], y = .data[[y_col]])) +
    geom_line(linewidth = 0.8) +
    geom_point(size = 2) +
    scale_x_datetime(date_labels = "%H:%M:%S") +
    labs(x = "time (UTC)", y = y_label) +
    theme_minimal()
}

# ------------ Data source -----------------------

# The data comes from the Express API in server.js: GET /api/<sensor>?limit=N
# Inside docker-compose the API is the "web" service; from RStudio it's localhost.
# Set API_URL to override (e.g. "https://your-site.com").
API_CANDIDATES <- c(Sys.getenv("API_URL"), "http://web:3000", "http://localhost:3000")
API_CANDIDATES <- API_CANDIDATES[API_CANDIDATES != ""]
LIMIT <- 100          # rows per sensor to fetch
REFRESH_MS <- 10000   # re-fetch every 10 s

# sensors and their plottable columns + units (the API doesn't send units)
# names must match the tables in db/schema.sql (<sensor>_data) and their columns
sensor_units <- list(
  bme280 = list(temperature = "C", humidity = "%RH", pressure = "hPa"),
  sgp30  = list(eco2 = "ppm", tvoc = "ppb"),
  bh1750 = list(lux = "lx")
)

# fetch_sensor() pulls the newest `limit` rows for one sensor and returns a data frame
# with a POSIXct `time` column + that sensor's metric columns (id/sensor_id/loc dropped)
fetch_sensor <- function(sensor, limit = LIMIT)
{
  metrics <- names(sensor_units[[sensor]])
  for (base in API_CANDIDATES) {
    url <- sprintf("%s/api/%s?limit=%d", base, sensor, limit)
    rows <- tryCatch(fromJSON(url, simplifyVector = TRUE), error = function(e) NULL)
    if (is.null(rows)) next                      # this address didn't answer, try the next one
    if (length(rows) == 0) break                 # API answered but the table is empty
    rows <- as.data.frame(rows)
    rows$time <- rows$recorded_at
    return(format_time(rows[, c("time", metrics)]))
  }
  # empty frame with the right columns so the UI still works
  empty <- data.frame(time = as.POSIXct(character(0), tz = "UTC"))
  for (m in metrics) empty[[m]] <- numeric(0)
  empty
}

# ----------------- UI ----------------------
ui <- fluidPage(
  selectInput("sensor", "Sensor", choices = names(sensor_units), width = "200px"),
  selectInput("metric", "Metric", choices = NULL, width = "200px"),
  plotOutput("sensor_plot", height = "500px")
)

# ----------------- SHINY server ------------------
server <- function(input, output, session) 
{
  # when the sensor changes, list that sensor's metrics as the choices
  observeEvent(input$sensor, 
{
    updateSelectInput(session, "metric", choices = names(sensor_units[[input$sensor]]))
  })

  # readings for the selected sensor, re-fetched from the API every REFRESH_MS
  readings <- reactive({
    invalidateLater(REFRESH_MS)
    fetch_sensor(req(input$sensor))
  })

  output$sensor_plot <- renderPlot({
    data <- readings()
    req(input$metric %in% names(data))   # skip the moment between switching sensors
    shiny::validate(need(nrow(data) > 0, "No readings yet for this sensor (or the API is unreachable)."))  # shiny:: because jsonlite::validate masks it

    plot_sensor(data, "time", input$metric,
                paste0(input$metric, " (", sensor_units[[input$sensor]][[input$metric]], ")"))
  })

}
options(browser = "firefox") # trouble shooting: if it doesn't load remove the comment and replace with specific browser
shinyApp(ui, server)
