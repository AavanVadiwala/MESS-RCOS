FROM debian:trixie-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends r-cran-shiny r-cran-ggplot2 r-cran-jsonlite \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /srv/shiny-server
COPY app.R ./
USER nobody
EXPOSE 3838
CMD ["Rscript", "-e", "shiny::runApp('.', host = '0.0.0.0', port = 3838L)"]
