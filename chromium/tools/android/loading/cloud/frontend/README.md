# Appengine Frontend for Clovis

[TOC]

## Usage

Visit the application URL in your browser, and upload a JSON dictionary with the
following keys:

-   `action` (string): the action to perform. Only `trace` and `report` are
    supported.
-   `action_params` (dictionary): the parameters associated to the action.
    See below for more details.
-   `backend_params` (dictionary): the parameters configuring the backend for
    this task. See below for more details.

### Parameters for `backend_params`

-   `instance_count` (int): Number of Compute Engine instances that will be
    started for this task.
-   `storage_bucket` (string): Name of the storage bucket used by the backend
    instances. Backend code and data must have been previously deployed to this
    bucket using the `deploy.sh` [script][4].
-   `tag` (string, optional): tag internally used to associate tasks to backend
    ComputeEngine instances. This parameter should not be set in general, as it
    is mostly exposed for development purposes. If this parameter is not
    specified, a unique tag will be generated.
-   `timeout_hours` (int, optional): if workers are still alive after this
    delay, they will be forcibly killed, to avoid wasting Compute Engine
    resources. Defaults to `5`.

### Parameters for the `trace` action

The trace action takes a list of URLs as input and generates a list of traces by
running Chrome.

-   `urls` (list of strings): the list of URLs to process.
-   `repeat_count` (integer, optional): the number of traces to be generated
    for each URL. Defaults to 1.
-   `emulate_device` (string, optional): the device to emulate (e.g. `Nexus 4`).
-   `emulate_network` (string, optional): the network to emulate.

### Parameters for the `report` action

Finds all the traces in the bucket (specified in the backend parameters) and
generates a report in BigQuery.

This action has no parameters.

## Development

### Design overview

-   Appengine configuration:
    -   `app.yaml` defines the handlers. There is a static handler for all URLs
    in the `static/` directory, and all other URLs are handled by the
    `clovis_frontend.py` script.
    -   `queue.yaml` defines the task queues associated with the application. In
        particular, the `clovis-queue` is a pull-queue where tasks are added by
        the AppEngine frontend and consummed by the ComputeEngine backend.
        See the [TaskQueue documentation][2] for more details.
-   `static/form.html` is a static HTML document allowing the user to upload a
    JSON file. `clovis_frontend.py` is then invoked with the contents of the
    file (see the `/form_sent` handler).
-   `clovis_task.py` defines a task to be run by the backend. It is sent through
    the `clovis-queue` task queue.
-   `clovis_frontend.py` is the script that processes the file uploaded by the
    form, creates the tasks and enqueues them in `clovis-queue`.

### Prerequisites

-   Install the gcloud [tool][1]
-   Add a `queue.yaml` file in the application directory (i.e. next to
    `app.yaml`) defining a `clovis-queue` pull queue that can be accessed by the
    ComputeEngine service worker associated to the project. Add your email too
    if you want to run the application locally. See the [TaskQueue configuration
    documentation][3] for more details. Example:

```
# queue.yaml
- name: clovis-queue
  mode: pull
  acl:
    - user_email: me@address.com
    - user_email: 123456789-compute@developer.gserviceaccount.com
```

### Run Locally

```shell
# Install dependencies in the lib/ directory. Note that this will pollute your
# Chromium checkout, see the cleanup intructions below.
pip install -r requirements.txt -t lib
# Start the local server.
dev_appserver.py -A $PROJECT_NAME .
```

Visit the application [http://localhost:8080](http://localhost:8080).

After you are done, cleanup your Chromium checkout:

```shell
rm -rf $CHROMIUM_SRC/tools/android/loading/frontend/lib
```

### Deploy

```shell
# Install dependencies in the lib/ directory.
pip install -r requirements.txt -t lib
# Deploy.
gcloud preview app deploy app.yaml
```

[1]: https://cloud.google.com/sdk
[2]: https://cloud.google.com/appengine/docs/python/taskqueue
[3]: https://cloud.google.com/appengine/docs/python/config/queue
[4]: ../backend/README.md#Deploy-the-code
