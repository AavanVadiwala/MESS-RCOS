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

# ------ helper functions ------------

# format_temperature() is a method that formats a time column with a specific format
format_temperature <- function(table)
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
    scale_x_datetime(date_labels = "%S") +
    labs(x = "time (s)", y = y_label) +
    theme_minimal()
}

# ------------ Data source -----------------------

# DATA_SOURCE <- "https://your-site.com/api/sensors" %this is used for the site 
DATA_SOURCE <- "dummy_data.json"
raw_data <- fromJSON(DATA_SOURCE, simplifyVector = TRUE)

# ------- Manually pulling data (only 3 sensors + for more specification and control) -------

# setting up bme280 data
bme280_units_array <- raw_data$bme280$unit
bme280_readings_table <- raw_data$bme280$readings
bme280_readings_table <- format_temperature(bme280_readings_table)

# setting up sgp30 data
sgp30_units_array <- raw_data$sgp30$unit
sgp30_readings_table <- raw_data$sgp30$readings
sgp30_readings_table <- format_temperature(sgp30_readings_table)

# setting up bh1750 data
bh1750_units_array <- raw_data$bh1750$unit
bh1750_readings_table <- raw_data$bh1750$readings
bh1750_readings_table <- format_temperature(bh1750_readings_table)

#adding sensors manually
sensors <- list(
  bme280 = list(readings = bme280_readings_table, units = bme280_units_array),
  sgp30  = list(readings = sgp30_readings_table,  units = sgp30_units_array),
  bh1750 = list(readings = bh1750_readings_table, units = bh1750_units_array)
)

# ----------------- UI ----------------------
ui <- fluidPage(
  selectInput("sensor", "Sensor", choices = names(sensors), width = "200px"),
  selectInput("metric", "Metric", choices = NULL, width = "200px"),
  plotOutput("sensor_plot", height = "500px")
)

# ----------------- SHINY server ------------------
server <- function(input, output, session) {

  # when the sensor changes, list every column except time as the metric choices
  observeEvent(input$sensor, {
    metrics <- setdiff(names(sensors[[input$sensor]]$readings), "time")
    updateSelectInput(session, "metric", choices = metrics)
  })

  output$sensor_plot <- renderPlot({
    s <- sensors[[req(input$sensor)]]
    req(input$metric %in% names(s$readings))   # skip the moment between switching sensors

    plot_sensor(s$readings, "time", input$metric,
                paste0(input$metric, " (", s$units[[input$metric]], ")"))
  })

}
#options(browser = "firefox") %trouble shooting: if it doesn't load remove the comment and replace with specific browser
shinyApp(ui, server)
