return office.product {
  schema_version = 1,

  identity = {
    slug = "office-demo",
    display_name = "Office Demo",
    description = "A neutral office dashboard reference product.",
    author = "MrMaxie",
    output_basename = "office-demo",
    title_id = "0x000400000FF40A00",
    product_code = "CTR-P-O3DE"
  },

  presentation = {
    palette = {
      accent = "#3B82A6",
      background = "#1C2329",
      control = "#2A343C",
      focus = "#E2A93B",
      foreground = "#F0F4F6",
      muted = "#7E8B94",
      surface_high = "#3A4852",
      track = "#11171B",
      success = "#54B887",
      text = "#F7FAFC"
    },
    copy = {
      activity = "Activity",
      absences = "Absences",
      recognition = "Recognition",
      worklog = "Worklog"
    },
    assets = {
      icon = "assets/icon.png",
      office_model_source = "assets/models/office/layers/layer_01.png",
      prelogin_ground = "assets/tiles/prelogin-ground.png",
      prelogin_skybox = "assets/ui/prelogin-skybox.png",
      prelogin_background = "assets/dashboard-background.png",
      login_background = "assets/session-background.png",
      top_background = "assets/session-background.png",
      bottom_background = "assets/dashboard-background.png",
      office_diorama = "assets/ui/office-diorama.png",
      worklog_icon = "assets/ui/icon-worklog.png",
      today_icon = "assets/ui/icon-today.png",
      team_icon = "assets/ui/icon-team.png",
      connection_icon = "assets/ui/icon-connection.png"
    }
  },

  backend = {
    api_base_url = office.required_value("api_base_url"),
    auth = {
      kind = "bearer_file",
      token_file_env = "OFFICE_3DS_DEMO_TOKEN_FILE",
      expiry = "token_expiry"
    },
    operations = {
      profile = office.request {
        method = "GET",
        path = "/v1/profile",
        response = office.object {
          id = office.string(office.field("id")),
          display_name = office.string(office.field("display_name")),
          role = office.optional(office.string(office.field("role")), ""),
          avatar_url = office.optional(office.string(office.field("avatar_url")), "")
        }
      },
      worklog = office.request {
        method = "GET",
        path = "/v1/worklog",
        query = { week = office.url_encode(office.context("date")) },
        depends_on = { "profile" },
        response = office.list(office.object {
          date = office.date(office.field("date")),
          minutes = office.number(office.field("minutes")),
          expected_minutes = office.default(office.number(office.field("expected_minutes")), 0)
        }, office.field("items"))
      },
      absences = office.request {
        method = "GET",
        path = "/v1/absences",
        response = office.list(office.object {
          id = office.string(office.field("id")),
          person_name = office.string(office.field("person_name")),
          starts_on = office.date(office.field("starts_on")),
          ends_on = office.date(office.field("ends_on")),
          return_date = office.optional(office.date(office.field("return_date")), ""),
          label = office.string(office.field("label")),
          group = office.optional(office.string(office.field("group")), ""),
          avatar_url = office.optional(office.string(office.field("avatar_url")), "")
        }, office.field("items"))
      },
      activity = office.request {
        method = "GET",
        path = "/v1/activity",
        response = office.list(office.object {
          id = office.string(office.field("id")),
          recipient_id = office.string(office.field("recipient_id")),
          recipient_name = office.string(office.field("recipient_name")),
          occurred_at = office.date(office.field("occurred_at")),
          age_label = office.optional(office.string(office.field("age_label")), ""),
          summary = office.string(office.field("summary")),
          allowed_recognition_values = office.field("allowed_recognition_values"),
          avatar_url = office.optional(office.string(office.field("avatar_url")), "")
        }, office.field("items"))
      },
      recognition = office.request {
        method = "POST",
        path = "/v1/recognitions",
        headers = { ["Content-Type"] = "application/json" },
        body = office.object {
          source_event_id = office.number(office.context("activity_id")),
          recipient_id = office.context("recipient_id"),
          value = office.context("value"),
          comment = office.context("message"),
          request_id = office.context("request_id")
        },
        response = office.object {
          accepted = office.default(office.field("accepted"), true),
          recognition_id = office.string(office.field("id"))
        }
      }
    }
  },

  extensions = {
    product_api_version = 1,
    adapter = "generated"
  }
}
